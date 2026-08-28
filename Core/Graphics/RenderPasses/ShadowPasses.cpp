#include "stdafx.h"
#include "ShadowPasses.h"
#include "ShadowCullPass.h"
#include "ShadowPass.h"
#include "Graphics/FrameGraph/FrameGraph.h"

using namespace Core;

ShadowPasses::ShadowPasses(FrameGraph& graph, Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
	VkFormat depthFormat)
{
	auto shadowPass = make_unique<ShadowPass>(device, resourceManager, renderScene, depthFormat);
	_shadowPass = shadowPass.get();

	graph.AddPass(make_unique<ShadowCullPass>(device, resourceManager, renderScene, *_shadowPass));
	graph.AddPass(std::move(shadowPass));
}

ShadowUniform* ShadowPasses::GetShadowBuffer() const
{
	return _shadowPass->GetShadowBuffer();
}
