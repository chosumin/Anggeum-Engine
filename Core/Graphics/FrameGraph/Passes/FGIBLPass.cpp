#include "stdafx.h"
#include "FGIBLPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "Graphics/RendererPasses/PreEnvironmentPass.h"
#include "Graphics/RendererPasses/BrdfLutPass.h"
#include "Foundation/Scene.h"

using namespace Core;

FGIBLPass::FGIBLPass(Device& device, Scene& scene)
	: _device(device)
	, _scene(scene)
{
}

FGIBLPass::~FGIBLPass() = default;

void FGIBLPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	frameResources.GetOrCreateUniformBuffer<GI>(UB_GI).Get().Update(_giBuffer);

	CreateResources(builder, frameResources, renderFrame);
}

void FGIBLPass::CreateResources(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	// After the one generating frame this declares nothing, so the graph culls the
	// pass and it costs no command buffer or submit.
	if (_generated)
		return;

	// Offscreen (rendered per cubemap face, then copied into the cubemaps)
	RenderTargetDesc offscreenDesc{};
	offscreenDesc.extent = { 128, 128 };
	offscreenDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	offscreenDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	offscreenDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	offscreenDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	auto offscreenHandle = frameResources.GetOrCreateRenderTarget(RT_OFFSCREEN, offscreenDesc);

	// Irradiance cubemap
	RenderTargetDesc irradianceDesc{};
	irradianceDesc.extent = { 128, 128 };
	irradianceDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	irradianceDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	irradianceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	irradianceDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	irradianceDesc.isCubemap = true;
	irradianceDesc.mipLevels = 8;
	irradianceDesc.arrayLayers = 6;
	auto irradianceHandle = frameResources.GetOrCreateRenderTarget(RT_IRRADIANCE, irradianceDesc);

	// Prefiltered cubemap
	RenderTargetDesc prefilteredDesc{};
	prefilteredDesc.extent = { 128, 128 };
	prefilteredDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	prefilteredDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	prefilteredDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	prefilteredDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	prefilteredDesc.isCubemap = true;
	prefilteredDesc.mipLevels = 8;
	prefilteredDesc.arrayLayers = 6;
	auto prefilteredHandle = frameResources.GetOrCreateRenderTarget(RT_PREFILTERED, prefilteredDesc);

	// BRDF LUT
	RenderTargetDesc brdfLutDesc{};
	brdfLutDesc.extent = { 512, 512 };
	brdfLutDesc.format = VK_FORMAT_R16G16_SFLOAT;
	brdfLutDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	brdfLutDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	brdfLutDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	auto brdfLutHandle = frameResources.GetOrCreateRenderTarget(RT_BRDF_LUT, brdfLutDesc);

	// Import + WriteManual: the generators do their own layout transitions
	// (UNDEFINED discard → attachment/transfer → shader-read), so the graph
	// emits no barriers and only needs them resolvable through the context.
	auto importManual = [&](const char* name, Handle<Texture> handle, TextureAccess finalState)
	{
		FGTexture t = builder.ImportTexture(name, handle);
		builder.WriteManual(t, finalState);
		return t;
	};
	_offscreen = importManual(RT_OFFSCREEN, offscreenHandle, TextureAccess::ColorWrite);
	_irradiance = importManual(RT_IRRADIANCE, irradianceHandle, TextureAccess::SampledFragment);
	_prefiltered = importManual(RT_PREFILTERED, prefilteredHandle, TextureAccess::SampledFragment);
	_brdfLut = importManual(RT_BRDF_LUT, brdfLutHandle, TextureAccess::SampledFragment);

	// The generators open a rendering scope per cubemap face, which the declared
	// attachment path cannot express.
	builder.SetManualRendering();

	// The results are consumed through bindless, which the graph cannot see.
	builder.SetSideEffect();

	_preEnvironmentPass = make_unique<PreEnvironmentPass>(_device, _scene, offscreenDesc.format);
	_preEnvironmentPass->Initialize();

	_brdfLutPass = make_unique<BrdfLutPass>(_device, brdfLutDesc.format);
	_brdfLutPass->Initialize();

	RegisterGiTexturesToBindless(renderFrame, irradianceHandle, prefilteredHandle, brdfLutHandle);

	// The indices only exist from here on, so the upload at the top of Setup ran
	// with zeros this frame; refresh the buffer the geometry pass will read.
	frameResources.GetOrCreateUniformBuffer<GI>(UB_GI).Get().Update(_giBuffer);

	_generated = true;
	_record = true;
}

void FGIBLPass::RegisterGiTexturesToBindless(RenderFrame& renderFrame,
	Handle<Texture> irradiance, Handle<Texture> prefiltered, Handle<Texture> brdfLut)
{
	if (!renderFrame.HasBindlessSupport())
		return;

	auto* bindlessManager = renderFrame.GetBindlessTextureManager();

	uint32_t irradianceCubemapIndex = bindlessManager->RegisterTexture(irradiance);
	uint32_t prefilteredCubemapIndex = bindlessManager->RegisterTexture(prefiltered);
	uint32_t brdfLutIndex = bindlessManager->RegisterTexture(brdfLut);

	// Strip the MSB cubemap flag before passing to the shader.
	// The bindless index stores BindlessCubemapFlag as a cubemap marker internally,
	// but the shader uses the value as a direct array index (no flags expected).
	_giBuffer.irradianceMapIndex = irradianceCubemapIndex & ~BindlessCubemapFlag;
	_giBuffer.prefilterMapIndex  = prefilteredCubemapIndex & ~BindlessCubemapFlag;
	_giBuffer.brdfLUTIndex       = brdfLutIndex & ~BindlessCubemapFlag;

	std::cout << "GI textures registered to bindless:" << endl;
	std::cout << "  Irradiance cubemap: index " << _giBuffer.irradianceMapIndex << endl;
	std::cout << "  Prefiltered cubemap: index " << _giBuffer.prefilterMapIndex << endl;
	std::cout << "  BRDF LUT: index " << _giBuffer.brdfLUTIndex << endl;
}

void FGIBLPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_record)
		return;

	_record = false;

	commandBuffer.BeginDebugMarker("IBL Generation");
	_preEnvironmentPass->Record(context, commandBuffer,
		context.GetTexture(_offscreen),
		context.GetTexture(_irradiance),
		context.GetTexture(_prefiltered));
	_brdfLutPass->Record(commandBuffer, context.GetTexture(_brdfLut));
	commandBuffer.EndDebugMarker();
}
