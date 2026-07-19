#include "stdafx.h"
#include "LightCullingPass.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Foundation/Scene.h"

Core::LightCullingPass::LightCullingPass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene, VkExtent2D swapChainExtents, ivec2 tileNums)
	:RendererPass(device, workerThreadManager), _scene(scene)
{
	_computeMaterial = device.GetResourceCache().RequestMaterial("lightCulling", "shaders/lightCulling.comp.spv");
	_computePipeline = make_unique<Core::Pipeline>(device, _computeMaterial->GetShader());

	_tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
	_tileInfo.tileNums = tileNums;
}

Core::LightCullingPass::~LightCullingPass()
{
}

void Core::LightCullingPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
	auto depthTarget = renderFrame.GetCurrentDepth();
	if (!depthTarget)
		return;

	// Wait for graphics queue (ResolvePass) to finish producing the resolved depth
	renderFrame.GetCurrentSubmitInfo().AddWaitSemaphore(
		QueueType::Graphics,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	StorageBufferDesc desc{};
	desc.size = GetLightVisibilityBufferSize(_tileInfo.tileNums);
	auto& lightVisibilityBuffer =
		renderFrame.GetOrCreateStorageBuffer(SB_LIGHT_VISIBILITY, desc);

	auto& cameraBuffer = renderFrame.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA);
	auto& lightBuffer = renderFrame.GetOrCreateUniformBuffer<LightBuffer>(UB_LIGHTS);

	auto builder = renderFrame.CreateDescriptorSetBuilder(_computeMaterial->GetShader(), 0);
	builder.SetUniformBuffer(0, cameraBuffer);
	builder.SetStorageBuffer(1, lightVisibilityBuffer);
	builder.SetTextureBuffer(2, depthTarget);
	builder.SetUniformBuffer(3, lightBuffer);
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(_computePipeline.get());

	commandBuffer.BindDescriptorSet(
		_computePipeline->GetPipelineBindPoint(),
		_computeMaterial->GetShader(), resources);

	commandBuffer.PushConstants(_computeMaterial->GetShader(), 0, _tileInfo);

	commandBuffer.Dispatch(_tileInfo.tileNums.x, _tileInfo.tileNums.y, 1);
}

