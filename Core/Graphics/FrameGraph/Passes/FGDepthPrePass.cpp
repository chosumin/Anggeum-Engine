#include "stdafx.h"
#include "FGDepthPrePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

FGDepthPrePass::FGDepthPrePass(Device& device, Scene& scene, VkExtent2D extent,
	VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
	: _device(device)
	, _scene(scene)
	, _extent(extent)
	, _msaaSamples(msaaSamples)
{
	_pipelineState = make_unique<PipelineState>();
	_pipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;

	_depthNormalShader = _device.GetResourceManager().LoadShader("DepthNormal");

	PipelineRenderingDesc renderingDesc;
	renderingDesc.colorFormats = { VK_FORMAT_R8G8B8A8_UNORM };
	renderingDesc.depthFormat = depthFormat;
	_pipeline = make_unique<Pipeline>(device, renderingDesc, _depthNormalShader.Get(), *_pipelineState);
}

FGDepthPrePass::~FGDepthPrePass() = default;

void FGDepthPrePass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderExecutor& renderExecutor)
{
	// the MSAA normal is consumed only by FGResolvePass and dies within
	// the graph — a true transient, aliasable by the allocator.
	Handle<Texture> normalTexture;
	if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
	{
		FGTextureDesc normalDesc{};
		normalDesc.extent = _extent;
		normalDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		normalDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		normalDesc.samples = _msaaSamples;
		normalDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		_mainNormal = builder.CreateTexture(RT_MAIN_NORMAL, normalDesc);
	}
	else
	{
		RenderTargetDesc normalDesc{};
		normalDesc.extent = _extent;
		normalDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		normalDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		normalDesc.samples = _msaaSamples;
		normalDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		normalDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		normalTexture = frameResources.GetOrCreateRenderTarget(RT_MAIN_NORMAL, normalDesc);
		_mainNormal = builder.ImportTexture(RT_MAIN_NORMAL, normalTexture,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	RenderTargetDesc depthDesc{};
	depthDesc.extent = _extent;
	depthDesc.format = VK_FORMAT_UNDEFINED; // auto-selected depth format
	depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	depthDesc.samples = _msaaSamples;
	depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	auto depthTexture = frameResources.GetOrCreateRenderTarget(RT_MAIN_DEPTH, depthDesc);

	// Hi-Z resolve target written mid-pass by RenderExecutor (and per frame by
	// ResolvePass when MSAA). Same description as the legacy ResolvePass created.
	RenderTargetDesc resolvedDepthDesc{};
	resolvedDepthDesc.extent = _extent;
	resolvedDepthDesc.format = VK_FORMAT_R32_SFLOAT;
	resolvedDepthDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	resolvedDepthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	resolvedDepthDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	resolvedDepthDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	auto resolvedDepthTexture = frameResources.GetOrCreateRenderTarget(RT_RESOLVED_DEPTH, resolvedDepthDesc);

	if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
	{
		frameResources.SetCurrentDepth(depthTexture);
		frameResources.SetCurrentNormal(normalTexture);
	}

	_mainDepth = builder.ImportTexture(RT_MAIN_DEPTH, depthTexture,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
	_resolvedDepth = builder.ImportTexture(RT_RESOLVED_DEPTH, resolvedDepthTexture,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Variant 0: CLEAR phase (occlusion pass 1). Variant 1: LOAD phase (pass 2).
	FGAttachment color0;
	color0.texture = _mainNormal;
	color0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color0.clear.color = { {0.25f, 0.25f, 0.25f, 1.0f} };
	builder.SetColorAttachment(0, color0, 0);

	FGAttachment depth0;
	depth0.texture = _mainDepth;
	depth0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth0.clear.depthStencil = { 1.0f, 0 };
	builder.SetDepthAttachment(depth0, 0);

	FGAttachment color1 = color0;
	color1.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	builder.SetColorAttachment(0, color1, 1);

	FGAttachment depth1 = depth0;
	depth1.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	builder.SetDepthAttachment(depth1, 1);

	// RenderExecutor resolves the depth mid-pass with its own barriers and
	// leaves the resolved depth shader-readable for pass-2 culling.
	builder.WriteManual(_resolvedDepth, TextureAccess::SampledCompute);

	// The two-phase occlusion flow interleaves compute culling and rendering in
	// one command buffer, so the pass begins/ends rendering itself, and it writes
	// external state (RendererBatch indirect draw buffers, Hi-Z pyramid).
	builder.SetManualRendering();
	builder.SetSideEffect();

	// CPU-written frame input, imported so Execute can resolve it through the
	// context like every other resource.
	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformVertex);

	// Stashed per frame:
	// the executor caches one culler per (camera, batch), so the pass keeps its
	// own rather than the executor holding a single "current" slot.
	_culler = nullptr;
	if (PerspectiveCamera* camera = _scene.GetMainCamera())
		_culler = renderExecutor.PrepareOcclusionCuller(camera->Matrices);
}

void FGDepthPrePass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	// Nothing to draw this frame (no camera, no batch, or no draw commands).
	if (_culler == nullptr)
		return;

	commandBuffer.SetViewportAndScissor(context.GetRenderArea(0));

	auto& depthNormalShader = _depthNormalShader.Get();
	auto builder = context.CreateDescriptorSetBuilder(depthNormalShader, 0);
	builder.SetUniformBuffer(0, context.GetBuffer(_camera));

	auto& executor = context.GetRenderExecutor();
	executor.OcclusionCullAndDraw(
		commandBuffer,
		depthNormalShader, *_pipeline,
		*_culler,
		context,
		context.GetTexture(_mainNormal), context.GetTexture(_mainDepth),
		builder, nullptr,
		nullptr);
}
