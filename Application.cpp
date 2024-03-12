#include "stdafx.h"
#include "Application.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "timer.h"
#include "SampleScene.h"
#include "GeometryRenderPass.h"
#include "Material.h"
#include "RenderContext.h"

Application::Application(const ApplicationOptions& options)
{
	assert(options.window != nullptr && "Window must be valid");
	
	_lockSimulationSpeed = options.BenchmarkEnabled;

	_timer = make_unique<Core::Timer>();

	Core::Device::Instance().Initialize(*options.window);
}

bool Application::Prepare()
{
	auto& device = Core::Device::Instance();

	_renderContext = new Core::RenderContext(device);

	auto swapChainExtent = _renderContext->GetSurfaceExtent();
	auto& swapChain = _renderContext->GetSwapChain();

	_scene = new SampleScene((float)swapChainExtent.width, (float)swapChainExtent.height, _renderContext->GetCommandPool());

	_renderPass = new Core::GeometryRenderPass(device, swapChainExtent, *_scene, swapChain);
	_renderPass->Prepare(_renderContext->GetCommandPool());

	return true;
}

void Application::Update()
{
	auto deltaTime = static_cast<float>(_timer->tick<Core::Timer::Seconds>());

	_fps = 1.0f / deltaTime;
	_frameTime = deltaTime * 1000.0f;

	auto components = _scene->GetComponents<Core::Component>();
	for (auto component : components)
	{
		component->UpdateFrame(deltaTime);
	}

	//todo : update scene
	//todo : update gui
	//todo : update stats
}

void Application::Finish()
{
}

Application::~Application()
{
	delete(_renderPass);
	delete(_scene);
	delete(_renderContext);

	Core::Device::Instance().Delete();
	Core::Window::Instance().Delete();
}

void Application::DrawFrame()
{
	auto& commandBuffer = _renderContext->Begin();

	_renderPass->Draw(commandBuffer, 
		_renderContext->GetCurrentFrame(), 
		_renderContext->GetImageIndex());

	_renderContext->Submit(commandBuffer);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(Core::Device::Instance().GetDevice());
}
