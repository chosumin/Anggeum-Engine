#include "stdafx.h"
#include "Application.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Triangle.h"
#include "MVPUniformBuffer.h"
#include "Texture.h"

Application::Application()
{
	Core::Window::Instance().Initialize(800, 600, "Hello Vulkan");
	Core::Device::Instance().Initialize(Core::Window::Instance());

	_swapChain = new Core::SwapChain();
	_commandBuffer = new Core::CommandBuffer();

	auto swapCahinExtent = _swapChain->GetSwapChainExtent();
	_mvpBuffer = new Core::MVPUniformBuffer((float)swapCahinExtent.width, (float)swapCahinExtent.height);

	_triangle = new Triangle(_commandBuffer->GetCommandPool());

	_texture = new Core::Texture(_commandBuffer->GetCommandPool(),
		"Textures/Anggeum.jpg", Core::TextureFormat::Rgb_alpha);

	_pipeline = new Core::Pipeline(
		_swapChain->GetRenderPass(),
		"shaders/simple_vs.vert.spv", "shaders/simple_fs.frag.spv",
		{ _mvpBuffer, _texture });
}

Application::~Application()
{
	delete(_pipeline);

	delete(_texture);

	delete(_mvpBuffer);
	delete(_triangle);

	delete(_swapChain);
	delete(_commandBuffer);

	Core::Device::Instance().Delete();
	Core::Window::Instance().Delete();
}

void Application::DrawFrame()
{
	_commandBuffer->RecordCommandBuffer(*_pipeline, *_swapChain);
	{
		_mvpBuffer->Update(
			_commandBuffer->GetCommandBuffer(),
			_commandBuffer->GetCurrentFrame());

		_triangle->DrawFrame(_commandBuffer->GetCommandBuffer());
	}
	_commandBuffer->EndFrame(*_pipeline, *_swapChain);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(Core::Device::Instance().GetDevice());
}
