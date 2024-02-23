#include "stdafx.h"
#include "SampleRenderPass.h"

Core::SampleRenderPass::SampleRenderPass(Device& device)
	:RenderPass(device)
{
	CreateColorRenderTarget
}

Core::SampleRenderPass::~SampleRenderPass()
{
}
