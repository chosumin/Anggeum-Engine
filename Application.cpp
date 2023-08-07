#include "stdafx.h"
#include "Application.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Polygon.h"
#include "MVPUniformBuffer.h"
#include "Texture.h"

Application::Application()
{
	Core::Window::Instance().Initialize(800, 600, "Hello Vulkan");
	Core::Device::Instance().Initialize(Core::Window::Instance());

	_swapChain = new Core::SwapChain();

	auto swapCahinExtent = _swapChain->GetSwapChainExtent();
	_mvpBuffer = new Core::MVPUniformBuffer((float)swapCahinExtent.width, (float)swapCahinExtent.height);

	_commandBuffer = new Core::CommandBuffer();

	_texture = new Core::Texture(_commandBuffer->GetCommandPool(),
		"Textures/Anggeum.jpg", Core::TextureFormat::Rgb_alpha);

	_pipeline = new Core::Pipeline(
		_swapChain->GetRenderPass(),
		"shaders/simple_vs.vert.spv", "shaders/simple_fs.frag.spv",
		{ _mvpBuffer, _texture });

	_polygon = new Core::Polygon(_commandBuffer->GetCommandPool(), vec3());
}

Application::~Application()
{
	delete(_texture);
	delete(_polygon);
	delete(_mvpBuffer);
	delete(_commandBuffer);
	delete(_pipeline);
	delete(_swapChain);

	Core::Device::Instance().Delete();
	Core::Window::Instance().Delete();
}

void Application::DrawFrame()
{
	_commandBuffer->RecordCommandBuffer(*_pipeline, *_swapChain, *_mvpBuffer, *_polygon);
	{
		/*_mvpBuffer->Update(_commandBuffer->GetCurrentFrame());

		_polygon->DrawFrame(_commandBuffer->GetCommandBuffer());*/
	}
	_commandBuffer->EndFrame(*_pipeline, *_swapChain);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(Core::Device::Instance().GetDevice());
}
