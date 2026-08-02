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

FGResolvePass::FGResolvePass(Device& device, VkExtent2D screenExtent,
	VkSampleCountFlagBits msaaSamples, bool resolveNormal)
	: _device(device)
	, _screenExtent(screenExtent)
	, _msaaSamples(msaaSamples)
	, _resolveNormal(resolveNormal)
{
	assert((!resolveNormal || msaaSamples != VK_SAMPLE_COUNT_1_BIT) &&
		"the normal resolve is MSAA-only");

	auto& resourceManager = _device.GetResourceManager();

	_depthResolveShader = resourceManager.LoadShader("Shaders/depthResolve.comp.spv");
	_depthResolvePipeline = resourceManager.LoadComputePipeline("Shaders/depthResolve.comp.spv");

	if (_resolveNormal)
	{
		_normalResolveShader = resourceManager.LoadShader("Shaders/normalResolve.comp.spv");
		_normalResolvePipeline = resourceManager.LoadComputePipeline("Shaders/normalResolve.comp.spv");
	}
}

FGResolvePass::~FGResolvePass() = default;

void FGResolvePass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	// FGDepthPrePass declared the MSAA targets in its Setup (declaration order
	// guarantees it ran first).
	_mainDepth = builder.GetTexture(FGDepthPrePass::RT_MAIN_DEPTH);

	// Depth history: next frame's pass-1 Hi-Z reads this slot's image, so it is
	// imported rather than a transient. Both instances declare it — ImportTexture
	// is idempotent for the same name and physical resource.
	RenderTargetDesc depthDesc{};
	depthDesc.extent = _screenExtent;
	depthDesc.format = VK_FORMAT_R32_SFLOAT;
	depthDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	depthDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	_resolvedDepth = builder.ImportTexture(RT_RESOLVED_DEPTH,
		frameResources.GetOrCreateRenderTarget(RT_RESOLVED_DEPTH, depthDesc));

	// The resolve shaders sample the source from compute-stage dispatches
	// recorded on the graphics queue.
	builder.Read(_mainDepth, TextureAccess::SampledCompute);
	builder.Write(_resolvedDepth, TextureAccess::StorageComputeWrite);

	if (!_resolveNormal)
		return;

	FGTextureDesc normalDesc{};
	normalDesc.extent = _screenExtent;
	normalDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
	normalDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	normalDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	normalDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	_resolvedNormal = builder.CreateTexture(RT_RESOLVED_NORMAL, normalDesc);

	_mainNormal = builder.GetTexture(FGDepthPrePass::RT_MAIN_NORMAL);

	builder.Read(_mainNormal, TextureAccess::SampledCompute);
	builder.Write(_resolvedNormal, TextureAccess::StorageComputeWrite);
}

void FGResolvePass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	// Layout transitions are the graph's job; the pass only dispatches.
	{
		commandBuffer.BeginDebugMarker("Resolve Depth");

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

		commandBuffer.BindPipeline(&_depthResolvePipeline.Get());
		commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, shader, resources);
		commandBuffer.PushConstants(shader, 0, pc);
		commandBuffer.Dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);

		commandBuffer.EndDebugMarker();
	}

	if (!_resolveNormal)
		return;

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

		commandBuffer.BindPipeline(&_normalResolvePipeline.Get());
		commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, shader, resources);
		commandBuffer.PushConstants(shader, 0, pc);
		commandBuffer.Dispatch((_screenExtent.width + 7) / 8, (_screenExtent.height + 7) / 8, 1);

		commandBuffer.EndDebugMarker();
	}
}
