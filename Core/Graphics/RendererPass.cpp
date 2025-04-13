#include "stdafx.h"
#include "RendererPass.h"
#include "Utils/Utility.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Framebuffer.h"
#include "Graphics/Vulkans/RenderPass.h"

Core::RendererPass::RendererPass(Device& device)
	:_device{ device }
{
	_renderPass = new RenderPass(device);
	_pipelineState = new PipelineState();
}

Core::RendererPass::~RendererPass()
{
	delete(_renderPass);
	delete(_pipelineState);
	delete(_framebuffer);
}

void Core::RendererPass::CreateFrameBuffer(SwapChain& swapChain)
{
	_framebuffer = new Framebuffer(_device, swapChain, *_renderPass);
}

void Core::RendererPass::CreateFrameBuffer(Image* image)
{
	_framebuffer = new Framebuffer(_device, *_renderPass, *image);
}
