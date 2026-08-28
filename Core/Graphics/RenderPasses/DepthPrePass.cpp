#include "stdafx.h"
#include "DepthPrePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "HiZCullPass.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

DepthPrePass::DepthPrePass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene, VkExtent2D extent,
	VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, Phase phase)
	: _device(device)
	, _renderScene(renderScene)
	, _extent(extent)
	, _msaaSamples(msaaSamples)
	, _phase(phase)
{
	_pipelineState = make_unique<PipelineState>();
	_pipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;

	_depthNormalShader = resourceManager.LoadShader("DepthNormal");

	PipelineRenderingDesc renderingDesc;
	renderingDesc.colorFormats = { VK_FORMAT_R8G8B8A8_UNORM };
	renderingDesc.depthFormat = depthFormat;
	_pipeline = make_unique<Pipeline>(device, renderingDesc, _depthNormalShader.Get(), *_pipelineState);
}

DepthPrePass::~DepthPrePass() = default;

void DepthPrePass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	if (_phase == Phase::First)
	{
		// MainNormal: within-frame transient (read by Resolve/AO).
		FGTextureDesc normalDesc{};
		normalDesc.extent = _extent;
		normalDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		normalDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		normalDesc.samples = _msaaSamples;
		normalDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		_mainNormal = builder.CreateTexture(RT_MAIN_NORMAL, normalDesc);

		// MainDepth: depth history (next frame's Hi-Z input via ResolvedDepth).
		RenderTargetDesc depthDesc{};
		depthDesc.extent = _extent;
		depthDesc.format = VK_FORMAT_UNDEFINED; // auto-selected depth format
		depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		depthDesc.samples = _msaaSamples;
		depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		auto depthTexture = frameResources.GetOrCreateRenderTarget(RT_MAIN_DEPTH, depthDesc);
		_mainDepth = builder.ImportTexture(RT_MAIN_DEPTH, depthTexture);
	}
	else
	{
		// Second phase draws into the targets the first phase created.
		_mainNormal = builder.GetTexture(RT_MAIN_NORMAL);
		_mainDepth = builder.GetTexture(RT_MAIN_DEPTH);
	}

	const bool first = _phase == Phase::First;
	const VkAttachmentLoadOp loadOp = first
		? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

	FGAttachment color;
	color.texture = _mainNormal;
	color.loadOp = loadOp;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clear.color = { {0.25f, 0.25f, 0.25f, 1.0f} };
	builder.SetColorAttachment(0, color);

	FGAttachment depth;
	depth.texture = _mainDepth;
	depth.loadOp = loadOp;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth.clear.depthStencil = { 1.0f, 0 };
	builder.SetDepthAttachment(depth);

	// CPU-written frame input, imported so Execute resolves it via the context.
	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformVertex);

	_indirect = FGBuffer{};
	const char* indirectName = _phase == Phase::First
		? HiZCullPass::SB_PASS1_INDIRECT
		: HiZCullPass::SB_PASS2_INDIRECT;
	if (builder.HasBuffer(indirectName))
	{
		_indirect = builder.GetBuffer(indirectName);
		builder.Read(_indirect, BufferAccess::IndirectRead);
	}
}

void DepthPrePass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	// Entered even with nothing to draw (no camera / no batch), so the declared
	// loadOps still clear the targets for the passes that read them.
	context.BeginRendering(commandBuffer);

	if (_indirect.IsValid())
	{
		commandBuffer.SetViewportAndScissor(context.GetRenderArea());

		auto& depthNormalShader = _depthNormalShader.Get();
		auto builder = context.CreateDescriptorSetBuilder(depthNormalShader, 0);
		builder.SetUniformBuffer(0, context.GetBuffer(_camera));

		_renderScene.DrawIndirect(commandBuffer, depthNormalShader, *_pipeline,
			context.GetBuffer(_indirect), builder);
	}

	context.EndRendering(commandBuffer);
}
