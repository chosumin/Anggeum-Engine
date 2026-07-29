#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "Foundation/Scene.h"
#include "Foundation/WorkerThread.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Components/Light.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/TransferJob.h"
#include "Graphics/Vulkans/SubmitInfo.h"
#include "Graphics/RendererPasses/DepthPrePass.h"
#include "Graphics/RendererPasses/LightCullingPass.h"
#include "Graphics/RendererPasses/GeometryPass.h"
#include "Graphics/RendererPasses/ShadowPass.h"
#include "Graphics/RendererPasses/SDFShadowPass.h"
#include "Graphics/RendererPasses/DFAOPass.h"
#include "Graphics/RendererPasses/CACAOPass.h"
#include "Graphics/RendererPasses/GUIRenderPass.h"
#include "Graphics/RendererPasses/AmbientOcclusionPass.h"
#include "Graphics/RendererPasses/ResolvePass.h"
#include "Utils/Utility.h"
using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(Device& device, 
	WorkerThreadManager& workerThreadManager,
	Scene& scene, SwapChain& swapChain)
	:_device(device)
	,_scene(scene)
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

	auto depthPrePass = new DepthPrePass(device, workerThreadManager, scene, swapChain, depthFormat, _msaaSamples);
	AddRendererPass(depthPrePass);

	if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
	{
		auto resolvePass = new ResolvePass(device, workerThreadManager, extent, _msaaSamples);
		AddRendererPass(resolvePass);
	}

	auto lightCullingPass = new LightCullingPass(device, workerThreadManager, scene, swapChain.GetSwapChainExtent(), tileNums);
	AddRendererPass(lightCullingPass);

	auto shadowPass = new ShadowPass(
		device, workerThreadManager, scene, depthFormat);
	AddRendererPass(shadowPass);

	auto sdfShadowPass = new SDFShadowPass(
		device, workerThreadManager, scene, extent, _msaaSamples, *shadowPass);
	AddRendererPass(sdfShadowPass);

	auto ambientOcclusionPass = new AmbientOcclusionPass(
		device, workerThreadManager, scene,
		extent, _msaaSamples,
		sdfShadowPass->GetSDFGenerator());
	AddRendererPass(ambientOcclusionPass);

	auto geometryPass = new GeometryPass(
		device, workerThreadManager, scene, swapChain, depthFormat, _msaaSamples,
		tileNums);
	AddRendererPass(geometryPass);

	auto guiPass = new GUIRenderPass(device, workerThreadManager, swapChain, _msaaSamples);
	AddRendererPass(guiPass);
}

Core::ForwardRenderPipeline::~ForwardRenderPipeline()
{
	Cleanup();

	for (auto&& rendererPass : _rendererPasses)
	{
		delete(rendererPass);
	}

	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::RemoveResizeCallback(a);
}

void ForwardRenderPipeline::Draw(RenderContext& renderContext, RenderFrame& renderFrame, uint32_t imageIndex)
{
	auto& queueTimer = renderContext.GetQueueTimer();
	const uint32_t frameIndex = renderContext.GetCurrentFrameIndex();

	UploadSharedUniforms(renderFrame);

	for (size_t passIndex = 0; passIndex < _rendererPasses.size(); passIndex++)
	{
		auto&& rendererPass = _rendererPasses[passIndex];

		// Get class name from typeid
		const char* className = typeid(*rendererPass).name();

		// Remove "class Core::" prefix if present
		const char* simpleName = className;
		const char* prefix = "class Core::";
		if (strncmp(className, prefix, strlen(prefix)) == 0)
		{
			simpleName = className + strlen(prefix);
		}

		// Request command buffer based on queue type
		QueueType queueType = rendererPass->GetQueueType();
		CommandBuffer& commandBuffer = (queueType == QueueType::Compute)
			? renderContext.RequestComputeCommandBuffer()
			: renderContext.RequestCommandBuffer();

		// Register SubmitInfo before Draw so the pass can inject wait/signal semaphores
		renderFrame.AddSubmitInfo(queueType, commandBuffer.GetHandle(),
			renderContext.GetSyncContext());

		commandBuffer.BeginCommandBuffer();
		queueTimer.BeginPass(commandBuffer, frameIndex,
			static_cast<uint32_t>(passIndex), queueType, simpleName);

		commandBuffer.BeginDebugMarker(simpleName);

		rendererPass->EnsureRenderTargets(renderFrame);
		rendererPass->Draw(renderFrame, commandBuffer, imageIndex);

		commandBuffer.EndDebugMarker();

		queueTimer.EndPass(commandBuffer, frameIndex, static_cast<uint32_t>(passIndex));
		commandBuffer.EndCommandBuffer();
	}
}

void Core::ForwardRenderPipeline::UploadSharedUniforms(RenderFrame& renderFrame)
{
	auto& frameResources = renderFrame.GetResources();
	if (auto* camera = _scene.GetMainCamera())
	{
		auto& cameraBuffer = frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA).Get();
		cameraBuffer.Update(camera->Matrices);
	}

	// Built CPU-side and assigned once: the mapping is uncached, so writing the
	// light array field by field into it would be slow.
	LightBuffer lights{};

	auto sceneLights = _scene.GetComponents<Light>();
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
	for (auto&& rendererPass : _rendererPasses)
	{
		// Get class name from typeid
		const char* className = typeid(*rendererPass).name();

		// Remove "class Core::" prefix if present
		const char* simpleName = className;
		const char* prefix = "class Core::";
		if (strncmp(className, prefix, strlen(prefix)) == 0)
		{
			simpleName = className + strlen(prefix);
		}

		rendererPass->OnGUI(renderFrame);
	}
	ImGui::End();
}

void Core::ForwardRenderPipeline::Cleanup()
{
}

void Core::ForwardRenderPipeline::Resize(SwapChain& swapChain)
{
	Cleanup();

	VkExtent2D extent = swapChain.GetSwapChainExtent();
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
