#include "stdafx.h"
#include "FGResolvePass.h"
#include "FGDepthPrePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Texture.h"

using namespace Core;

namespace
{
	struct ResolvePushConstants
	{
		int32_t outputWidth;
		int32_t outputHeight;
		int32_t sampleCount;
		int32_t padding;
	};
}

FGResolvePass::FGResolvePass(Device& device, VkExtent2D screenExtent, VkSampleCountFlagBits msaaSamples)
	: _device(device)
	, _screenExtent(screenExtent)
	, _msaaSamples(msaaSamples)
{
	assert(msaaSamples != VK_SAMPLE_COUNT_1_BIT && "FGResolvePass is MSAA-only");

	_depthResolveShader = _device.GetResourceManager().LoadShader("Shaders/depthResolve.comp.spv");
	_depthResolvePipeline = make_unique<Pipeline>(_device, _depthResolveShader.Get());

	_normalResolveShader = _device.GetResourceManager().LoadShader("Shaders/normalResolve.comp.spv");
	_normalResolvePipeline = make_unique<Pipeline>(_device, _normalResolveShader.Get());
}

FGResolvePass::~FGResolvePass() = default;

void FGResolvePass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderExecutor& renderExecutor)
{
	FGTextureDesc resolvedNormalDesc{};
	resolvedNormalDesc.extent = _screenExtent;
	resolvedNormalDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
	resolvedNormalDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	resolvedNormalDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	resolvedNormalDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	_resolvedNormal = builder.CreateTexture(RT_RESOLVED_NORMAL, resolvedNormalDesc);

	// FGDepthPrePass declared MainDepth/MainNormal/ResolvedDepth in its Setup
	// (declaration order guarantees it ran first).
	_mainDepth = builder.GetTexture(FGDepthPrePass::RT_MAIN_DEPTH);
	_mainNormal = builder.GetTexture(FGDepthPrePass::RT_MAIN_NORMAL);
	_resolvedDepth = builder.GetTexture(RT_RESOLVED_DEPTH);

	// The resolve shaders sample the MSAA targets from compute-stage dispatches
	// recorded on the graphics queue.
	builder.Read(_mainDepth, TextureAccess::SampledCompute);
	builder.Read(_mainNormal, TextureAccess::SampledCompute);
	builder.Write(_resolvedDepth, TextureAccess::StorageComputeWrite);
	builder.Write(_resolvedNormal, TextureAccess::StorageComputeWrite);
}

void FGResolvePass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	// Depth resolve. Layout transitions are the graph's job now — the pass only
	// dispatches.
	{
		commandBuffer.BeginDebugMarker("Resolve MSAA Depth");

		auto extent = context.GetTexture(_resolvedDepth).GetExtent();
		ResolvePushConstants pc{
			static_cast<int32_t>(extent.width),
			static_cast<int32_t>(extent.height),
			static_cast<int32_t>(_msaaSamples),
			0
		};

		auto& shader = _depthResolveShader.Get();
		auto builder = context.CreateDescriptorSetBuilder(shader, 0);
		builder.SetTextureBuffer(0, context.GetTexture(_mainDepth));
		builder.SetTextureBuffer(1, context.GetTexture(_resolvedDepth), 0, VK_IMAGE_LAYOUT_GENERAL);
		auto& resources = builder.Build();

		commandBuffer.BindPipeline(_depthResolvePipeline.get());
		commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, shader, resources);
		commandBuffer.PushConstants(shader, 0, pc);
		commandBuffer.Dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);

		commandBuffer.EndDebugMarker();
	}

	// Normal resolve.
	{
		commandBuffer.BeginDebugMarker("Resolve MSAA Normal");

		ResolvePushConstants pc{
			static_cast<int32_t>(_screenExtent.width),
			static_cast<int32_t>(_screenExtent.height),
			static_cast<int32_t>(_msaaSamples),
			0
		};

		auto& shader = _normalResolveShader.Get();
		auto builder = context.CreateDescriptorSetBuilder(shader, 0);
		builder.SetTextureBuffer(0, context.GetTexture(_mainNormal));
		builder.SetTextureBuffer(1, context.GetTexture(_resolvedNormal), 0, VK_IMAGE_LAYOUT_GENERAL);
		auto& resources = builder.Build();

		commandBuffer.BindPipeline(_normalResolvePipeline.get());
		commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, shader, resources);
		commandBuffer.PushConstants(shader, 0, pc);
		commandBuffer.Dispatch((_screenExtent.width + 7) / 8, (_screenExtent.height + 7) / 8, 1);

		commandBuffer.EndDebugMarker();
	}
}
