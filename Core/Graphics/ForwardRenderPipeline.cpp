#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "Foundation/Scene.h"
#include "Foundation/WorkerThread.h"
#include "Foundation/Entity.h"
#include "Components/Light.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"
#include "Graphics/FrameGraph/FrameGraph.h"
#include "Graphics/RenderPasses/HiZCullPass.h"
#include "Graphics/RenderPasses/DepthPrePass.h"
#include "Graphics/RenderPasses/ResolvePass.h"
#include "Graphics/RenderPasses/LightCullingPass.h"
#include "Graphics/RenderPasses/ShadowCullPass.h"
#include "Graphics/RenderPasses/ShadowPass.h"
#include "Graphics/RenderPasses/SDFShadowPass.h"
#include "Graphics/RenderPasses/AmbientOcclusionPass.h"
#include "Graphics/RenderPasses/IBLPass.h"
#include "Graphics/RenderPasses/GeometryPass.h"
#include "Graphics/RenderPasses/GUIRenderPass.h"
#include "Utils/Utility.h"
using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(Device& device, 
	WorkerThreadManager& workerThreadManager,
	RenderScene& renderScene, SwapChain& swapChain)
	:_device(device)
	,_renderScene(renderScene)
	,_swapChainExtents(swapChain.GetSwapChainExtent())
{
	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::AddResizeCallback(a);

	_msaaSamples = GetMaxUsableSampleCount();

	auto extent = swapChain.GetSwapChainExtent();

	ivec2 tileNums = ivec2(
		(extent.width - 1) / TILE_SIZE + 1,
		(extent.height - 1) / TILE_SIZE + 1);

	auto depthFormat = _device.FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	_frameGraph = make_unique<FrameGraph>(device, workerThreadManager);

	// Two-pass occlusion culling is interleaved (pass-2 culls against the depth
	// pass 1 drew), so the depth prepass is split and every step between the two
	// halves is its own pass:
	using CullPhase = HiZCullPass::Phase;
	using DepthPhase = DepthPrePass::Phase;

	// Cull1 owns the CPU state both culling phases share; Cull2 references it.
	auto hiZCull1 = make_unique<HiZCullPass>(device, renderScene, CullPhase::Cull1);
	HiZCullPass* hiZCull1Ptr = hiZCull1.get();

	_frameGraph->AddPass(std::move(hiZCull1));
	_frameGraph->AddPass(make_unique<DepthPrePass>(device, renderScene, extent, depthFormat, _msaaSamples, DepthPhase::First));
	_frameGraph->AddPass(make_unique<ResolvePass>(device, extent, _msaaSamples, /*resolveNormal*/ false));
	_frameGraph->AddPass(make_unique<HiZCullPass>(device, renderScene, CullPhase::Cull2, hiZCull1Ptr));
	_frameGraph->AddPass(make_unique<DepthPrePass>(device, renderScene, extent, depthFormat, _msaaSamples, DepthPhase::Second));
	if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
		_frameGraph->AddPass(make_unique<ResolvePass>(device, extent, _msaaSamples, /*resolveNormal*/ true));
	_frameGraph->AddPass(make_unique<LightCullingPass>(device, renderScene, extent, tileNums, _msaaSamples));

	// The cull pass runs first but reads the cascade matrices off the shadow
	// pass, so the shadow pass object is created before it and added after.
	auto fgShadowPass = make_unique<ShadowPass>(device, renderScene, depthFormat);
	ShadowPass* fgShadowPassPtr = fgShadowPass.get();
	_frameGraph->AddPass(make_unique<ShadowCullPass>(device, renderScene, *fgShadowPassPtr));
	_frameGraph->AddPass(std::move(fgShadowPass));

	auto fgSdfShadowPass = make_unique<SDFShadowPass>(
		device, renderScene, extent, _msaaSamples, *fgShadowPassPtr);
	SDFShadowPass* fgSdfShadowPassPtr = fgSdfShadowPass.get();
	_frameGraph->AddPass(std::move(fgSdfShadowPass));

	_frameGraph->AddPass(make_unique<AmbientOcclusionPass>(
		device, renderScene, extent, _msaaSamples,
		fgSdfShadowPassPtr->GetSDFGenerator()));

	// Generates the IBL maps the geometry pass samples. Runs on the first frame
	// only; after that it declares nothing and the graph culls it.
	_frameGraph->AddPass(make_unique<IBLPass>(device, renderScene));

	_frameGraph->AddPass(make_unique<GeometryPass>(
		device, renderScene, swapChain, depthFormat, _msaaSamples, tileNums));

	_frameGraph->AddPass(make_unique<GUIRenderPass>(device, swapChain, _msaaSamples));
}

Core::ForwardRenderPipeline::~ForwardRenderPipeline()
{
	Cleanup();

	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::RemoveResizeCallback(a);
}

void ForwardRenderPipeline::Draw(RenderContext& renderContext, RenderFrame& renderFrame, uint32_t imageIndex)
{
	UploadSharedUniforms(renderFrame);

	_frameGraph->SetupAndCompile(renderFrame, imageIndex);
	_frameGraph->Execute(renderContext, renderFrame);
}

void Core::ForwardRenderPipeline::UploadSharedUniforms(RenderFrame& renderFrame)
{
	auto& frameResources = renderFrame.GetResources();
	if (auto* camera = _renderScene.GetScene().GetMainCamera())
	{
		auto& cameraBuffer = frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA).Get();
		cameraBuffer.Update(camera->Matrices);
	}

	// Built CPU-side and assigned once: the mapping is uncached, so writing the
	// light array field by field into it would be slow.
	LightBuffer lights{};

	auto sceneLights = _renderScene.GetScene().GetComponents<Light>();
	uint32_t count = std::min((uint32_t)sceneLights.size(), (uint32_t)MAX_FORWARD_LIGHT_COUNT);
	for (uint32_t i = 0; i < count; ++i)
	{
		auto* light = sceneLights[i];

		auto& properties = light->GetProperties();
		auto& transform = light->GetEntity().GetTransform();

		LightInfo lightInfo{};
		lightInfo.Position = vec4(transform.GetTranslation(),
			static_cast<float>(light->GetLightType()));
		lightInfo.Color = vec4(properties.Color, properties.Intensity);

		auto direction = transform.GetRotation() * properties.Direction;
		lightInfo.Direction = vec4(direction, properties.Range);
		lightInfo.Info = vec2(properties.InnerConeAngle, properties.OuterConeAngle);

		lights.Light[i] = lightInfo;
	}
	lights.Count = count;

	auto& lightBuffer = frameResources.GetOrCreateUniformBuffer<LightBuffer>(UB_LIGHTS).Get();
	lightBuffer.Update(lights);
}

void Core::ForwardRenderPipeline::OnGUI(RenderFrame& renderFrame)
{
	ImGui::Begin("Renderer Passes");

	_frameGraph->OnDebugGUI();
	_frameGraph->OnGUI(renderFrame);

	ImGui::End();
}

void Core::ForwardRenderPipeline::Cleanup()
{
}

void Core::ForwardRenderPipeline::Resize(SwapChain& swapChain)
{
	Cleanup();

	_frameGraph->Invalidate();
}

VkSampleCountFlagBits Core::ForwardRenderPipeline::GetMaxUsableSampleCount()
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(
		_device.GetPhysicalDevice(),
		&physicalDeviceProperties);

	VkSampleCountFlags counts =
		physicalDeviceProperties.limits.framebufferColorSampleCounts &
		physicalDeviceProperties.limits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}
