#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "Foundation/Scene.h"
#include "Foundation/WorkerThread.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"
#include "Graphics/ResourceCache.h"
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
	CreateLightCullingBuffer(extent, tileNums);

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

	auto lightCullingPass = new LightCullingPass(device, workerThreadManager, scene, swapChain.GetSwapChainExtent(), tileNums, _lightBuffer);
	AddRendererPass(lightCullingPass);

	auto shadowPass = new ShadowPass(
		device, workerThreadManager, scene, depthFormat, _shadowBuffer);
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
		_shadowBuffer,
		_lightBuffer, tileNums);
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

	delete(_lightBuffer);

		auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::RemoveResizeCallback(a);
}

void ForwardRenderPipeline::Draw(RenderContext& renderContext, RenderFrame& renderFrame, uint32_t imageIndex)
{
	auto& queueTimer = renderContext.GetQueueTimer();
	const uint32_t frameIndex = renderContext.GetCurrentFrameIndex();

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

void Core::ForwardRenderPipeline::CreateLightCullingBuffer(VkExtent2D extent, ivec2 tileNums)
{
	u32 lightVisiblityBufferSize = sizeof(VisibleLightsForTile) * tileNums.x * tileNums.y;

	auto lightVisibilityBuffer = new Core::Buffer(_device,
		lightVisiblityBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	_lightBuffer = lightVisibilityBuffer;
}