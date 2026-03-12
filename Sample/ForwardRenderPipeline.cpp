#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "Foundation/Scene.h"
#include "Foundation/WorkerThread.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/RenderContext.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/TransferJob.h"
#include "Graphics/RendererPasses/DepthPrePass.h"
#include "Graphics/RendererPasses/LightCullingPass.h"
#include "Sample/RendererPasses/GeometryPass.h"
#include "Sample/RendererPasses/ShadowPass.h"
#include "Sample/RendererPasses/SDFShadowPass.h"
#include "Sample/RendererPasses/GUIRenderPass.h"
#include "Utils/Utility.h"
using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(Device& device, 
	WorkerThreadManager& workerThreadManager,
	Scene& scene, SwapChain& swapChain)
	:_device(device)
{
	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::AddResizeCallback(a);

	_msaaSamples = GetMaxUsableSampleCount();

	auto extent = swapChain.GetSwapChainExtent();

	ivec2 tileNums = ivec2(
		(extent.width - 1) / TILE_SIZE + 1,
		(extent.height - 1) / TILE_SIZE + 1);
	CreateLightCullingBuffer(extent, tileNums);

	CreateTransformBuffer(scene);

	auto depthFormat = _device.FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	auto depthPrePass = new DepthPrePass(device, workerThreadManager, scene, swapChain, depthFormat, _msaaSamples, _transformBatch);
	AddRendererPass(depthPrePass);

	auto shadowPass = new ShadowPass(
		device, workerThreadManager, scene, depthFormat, _shadowBuffer, _transformBatch);
	AddRendererPass(shadowPass);

	if (device.IsGpuDrivenRenderingEnabled())
	{
		auto sdfShadowPass = new SDFShadowPass(
			device, workerThreadManager, scene, extent, _msaaSamples);
		AddRendererPass(sdfShadowPass);

		auto* batches = shadowPass->GetRendererBatches();
		sdfShadowPass->SetGPUBoundsData(
			batches->GetObjectDataBuffer(),
			_transformBatch.TransformBuffer,
			batches->GetIndirectCommandBuffer(),
			batches->GetDrawCommandCount(),
			batches->GetInstanceCount());
	}
	
	auto lightCullingPass = new LightCullingPass(device, workerThreadManager, scene, swapChain.GetSwapChainExtent(), tileNums, _lightBuffer);
	AddRendererPass(lightCullingPass);

	auto geometryPass = new GeometryPass(
		device, workerThreadManager, scene, swapChain, depthFormat, _msaaSamples,
		_shadowBuffer,
		_lightBuffer, tileNums, _transformBatch);
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
	delete(_transformBatch.TransformBuffer);

	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::RemoveResizeCallback(a);
}

void ForwardRenderPipeline::Prepare()
{
	for (auto&& rendererPass : _rendererPasses)
	{
		rendererPass->Prepare();
	}
}

void ForwardRenderPipeline::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	auto& commandBuffer = renderFrame.GetCommandBuffer();

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

		// Begin debug marker for this render pass
		commandBuffer.BeginDebugMarker(simpleName);

		rendererPass->Draw(renderFrame, imageIndex);

		// End debug marker
		commandBuffer.EndDebugMarker();
	}
}

void Core::ForwardRenderPipeline::Cleanup()
{
}

void Core::ForwardRenderPipeline::Resize(SwapChain& swapChain)
{
	Cleanup();

	VkExtent2D extent = swapChain.GetSwapChainExtent();

	/*if (_color != nullptr)
	{
		CreateColorRenderTarget(extent, _color->Format, _color->LoadOp, _color->StoreOp, _color->InitialLayout);
	}

	if (_depth != nullptr)
	{
		CreateDepthRenderTarget(extent, _depth->LoadOp, _depth->StoreOp);
	}

	for (auto& renderTarget : _inputRenderTargets)
	{
		CreateRenderTarget(extent, renderTarget->Format, renderTarget->Layout, renderTarget->UsageFlags, renderTarget->LoadOp, renderTarget->StoreOp);
	}*/
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

void Core::ForwardRenderPipeline::CreateTransformBuffer(Scene& scene)
{
	auto meshes = scene.GetComponents<Core::Mesh>();
	
	size_t meshCount = meshes.size();
	uint bufferSize = sizeof(mat4) * meshCount;

	vector<mat4> transforms(meshCount);
	_transformBatch.EntityIds.resize(meshCount);

	for (size_t i = 0; i < meshCount; ++i)
	{
		auto& entity = meshes[i]->GetEntity();
		auto& transform = entity.GetTransform();
		transforms[i] = transform.GetMatrix();
		_transformBatch.EntityIds[i] = static_cast<uint>(entity.GetId());
	}

	_transformBatch.TransformBuffer = new Core::Buffer(_device,
		bufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	Core::VkBufferJob<mat4> job(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &_transformBatch.TransformBuffer, transforms, true);
	Core::CommandBuffer::ImmediateSubmit(_device, job);
}