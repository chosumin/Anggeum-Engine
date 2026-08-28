#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "Foundation/Scene.h"
#include "Foundation/WorkerThread.h"
#include "Foundation/Entity.h"
#include "Components/Light.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/RenderScene.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"
#include "Graphics/FrameGraph/FrameGraph.h"
#include "Graphics/RenderPasses/DepthPrePasses.h"
#include "Graphics/RenderPasses/LightCullingPass.h"
#include "Graphics/RenderPasses/ShadowPasses.h"
#include "Graphics/RenderPasses/SDFShadowPass.h"
#include "Graphics/RenderPasses/AmbientOcclusionPass.h"
#include "Graphics/RenderPasses/IBLPass.h"
#include "Graphics/RenderPasses/GeometryPass.h"
#include "Graphics/RenderPasses/GUIRenderPass.h"
#include "Graphics/RenderPasses/TerrainNodeListPass.h"
#include "Graphics/RenderPasses/TerrainLodMapPass.h"
#include "Graphics/RenderPasses/TerrainPass.h"
#include "Utils/Utility.h"
using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(Device& device, ResourceManager& resourceManager,
	WorkerThreadManager& workerThreadManager,
	RenderScene& renderScene, SwapChain& swapChain, SyncContext& syncContext)
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

	_frameGraph = make_unique<FrameGraph>(device, resourceManager, workerThreadManager);

	_frameGraph->AddPass(make_unique<TerrainNodeListPass>(device, resourceManager, renderScene));
	_frameGraph->AddPass(make_unique<TerrainLodMapPass>(device, resourceManager, renderScene));

	DepthPrePasses depthPrePasses(*_frameGraph, device, resourceManager, renderScene, extent, depthFormat, _msaaSamples);

	_frameGraph->AddPass(make_unique<LightCullingPass>(device, resourceManager, renderScene, extent, tileNums, _msaaSamples));

	// The shadow feature wires its own cull + draw passes into the graph.
	ShadowPasses shadowPasses(*_frameGraph, device, resourceManager, renderScene, depthFormat);

	auto fgSdfShadowPass = make_unique<SDFShadowPass>(
		device, resourceManager, renderScene, extent, _msaaSamples, shadowPasses.GetShadowBuffer());
	SDFShadowPass* fgSdfShadowPassPtr = fgSdfShadowPass.get();
	_frameGraph->AddPass(std::move(fgSdfShadowPass));

	_frameGraph->AddPass(make_unique<AmbientOcclusionPass>(
		device, resourceManager, renderScene, extent, _msaaSamples,
		fgSdfShadowPassPtr->GetSDFGenerator()));

	// Generates the IBL maps the geometry pass samples. Runs on the first frame
	// only; after that it declares nothing and the graph culls it.
	_frameGraph->AddPass(make_unique<IBLPass>(device, resourceManager, renderScene));

	_frameGraph->AddPass(make_unique<GeometryPass>(
		device, renderScene, swapChain, depthFormat, _msaaSamples, tileNums));

	_frameGraph->AddPass(make_unique<TerrainPass>(device, resourceManager, renderScene,
		swapChain.GetImageFormat(), depthFormat, _msaaSamples));

	_frameGraph->AddPass(make_unique<GUIRenderPass>(device, swapChain, _msaaSamples,
		syncContext));
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
