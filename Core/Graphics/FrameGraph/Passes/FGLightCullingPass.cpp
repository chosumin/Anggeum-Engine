#include "stdafx.h"
#include "FGLightCullingPass.h"
#include "FGDepthPrePass.h"
#include "FGResolvePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Material.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"

using namespace Core;

FGLightCullingPass::FGLightCullingPass(Device& device, Scene& scene,
	VkExtent2D swapChainExtents, ivec2 tileNums, VkSampleCountFlagBits msaaSamples)
	: _device(device)
	, _scene(scene)
	, _msaaSamples(msaaSamples)
{
	_computeMaterial = device.GetResourceManager().LoadMaterial("lightCulling", "shaders/lightCulling.comp.spv");
	_computePipeline = make_unique<Pipeline>(device, _computeMaterial.Get().GetShaderHandle().Get());

	_tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
	_tileInfo.tileNums = tileNums;
}

FGLightCullingPass::~FGLightCullingPass() = default;

void FGLightCullingPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderExecutor& renderExecutor)
{
	// The depth this pass consumes: the resolved depth when MSAA is on, the main
	// depth otherwise. Both were declared by the earlier graph passes.
	const char* depthName = _msaaSamples != VK_SAMPLE_COUNT_1_BIT
		? FGResolvePass::RT_RESOLVED_DEPTH
		: FGDepthPrePass::RT_MAIN_DEPTH;

	_depthHandle = frameResources.GetRenderTarget(depthName);
	_depth = builder.GetTexture(depthName);
	builder.Read(_depth, TextureAccess::SampledCompute);

	BufferDesc desc{};
	desc.size = GetLightVisibilityBufferSize(_tileInfo.tileNums);
	auto lightVisibilityHandle = frameResources.GetOrCreateStorageBuffer(SB_LIGHT_VISIBILITY, desc);
	_lightVisibilityBuffer = &lightVisibilityHandle.Get();

	// Legacy GeometryPass consumes this buffer with its own acquire barrier and
	// compute-timeline wait, so no export barrier is needed — importing it keeps
	// the pass alive and lets the registry carry cross-frame write state.
	_lightVisibility = builder.ImportBuffer(SB_LIGHT_VISIBILITY, lightVisibilityHandle);
	builder.Write(_lightVisibility, BufferAccess::StorageComputeWrite);

	_cameraBuffer = &frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA).Get();
	_lightBuffer = &frameResources.GetOrCreateUniformBuffer<LightBuffer>(UB_LIGHTS).Get();
}

void FGLightCullingPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	auto& computeShader = _computeMaterial.Get().GetShaderHandle().Get();

	auto builder = context.CreateDescriptorSetBuilder(computeShader, 0);
	builder.SetUniformBuffer(0, *_cameraBuffer);
	builder.SetStorageBuffer(1, *_lightVisibilityBuffer);
	builder.SetTextureBuffer(2, _depthHandle);
	builder.SetUniformBuffer(3, *_lightBuffer);
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(_computePipeline.get());
	commandBuffer.BindDescriptorSet(_computePipeline->GetPipelineBindPoint(),
		computeShader, resources);
	commandBuffer.PushConstants(computeShader, 0, _tileInfo);
	commandBuffer.Dispatch(_tileInfo.tileNums.x, _tileInfo.tileNums.y, 1);
}
