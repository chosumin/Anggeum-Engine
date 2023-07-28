#include "stdafx.h"
#include "Application.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Triangle.h"
#include "MVPUniformBuffer.h"

Application::Application()
{
	Core::Window::GetInstance().Initialize(800, 600, "Hello Vulkan");
	Core::Device::GetInstance().Initialize(Core::Window::GetInstance());

	_swapChain = new Core::SwapChain();
	_commandBuffer = new Core::CommandBuffer();

	auto swapCahinExtent = _swapChain->GetSwapChainExtent();
	_mvpBuffer = new Core::MVPUniformBuffer((float)swapCahinExtent.width, (float)swapCahinExtent.height);

	_triangle = new Triangle(_commandBuffer->GetCommandPool());

	_pipeline = new Core::Pipeline(
		_swapChain->GetRenderPass(), 
		_mvpBuffer->GetDescriptorSetLayout(),
		"shaders/simple_vs.vert.spv", "shaders/simple_fs.frag.spv");
}

Application::~Application()
{
	delete(_pipeline);

	delete(_mvpBuffer);
	delete(_triangle);

	delete(_swapChain);
	delete(_commandBuffer);

	Core::Device::GetInstance().Delete();
	Core::Window::GetInstance().Delete();
}

void Application::DrawFrame()
{
	_commandBuffer->WaitForFences(*_swapChain);
	_commandBuffer->RecordCommandBuffer(*_pipeline, *_swapChain);

	_mvpBuffer->Update(
		_commandBuffer->GetCommandBuffer(),
		_commandBuffer->GetCurrentFrame(),
		_pipeline->GetPipelineLayout());

	_triangle->DrawFrame(_commandBuffer->GetCommandBuffer());
	_commandBuffer->EndFrame(*_pipeline, *_swapChain);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(Core::Device::GetInstance().GetDevice());
}
