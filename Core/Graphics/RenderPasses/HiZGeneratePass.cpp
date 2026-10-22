#include "stdafx.h"
#include "HiZGeneratePass.h"
#include "HiZPyramid.h"
#include "ResolvePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/Vulkans/Texture.h"

using namespace Core;

HiZGeneratePass::HiZGeneratePass(ResourceManager& resourceManager, VkExtent2D screenExtent)
	: _pyramid(make_unique<HiZPyramid>(resourceManager, screenExtent))
{
}

HiZGeneratePass::~HiZGeneratePass() = default;

void HiZGeneratePass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	_active = false;

	// The last resolve of the frame wrote it; absent when no depth was drawn.
	if (!builder.HasTexture(ResolvePass::RT_RESOLVED_DEPTH))
		return;

	_depth = builder.GetTexture(ResolvePass::RT_RESOLVED_DEPTH);
	builder.Read(_depth, TextureAccess::TransferSrc);

	_hiZTexture = _pyramid->EnsureTexture(frameResources, RT_HIZ_FINAL);
	FGTexture hiZ = builder.ImportTexture(RT_HIZ_FINAL, _hiZTexture);
	builder.WriteManual(hiZ, TextureAccess::SampledCompute);

	_active = true;
}

void HiZGeneratePass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	_pyramid->Build(context, commandBuffer, context.GetTexture(_depth), _hiZTexture.Get());
}
