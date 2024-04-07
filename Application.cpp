#include "stdafx.h"
#include "Application.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "timer.h"
#include "SampleScene.h"
#include "ForwardRenderPipeline.h"
#include "RenderContext.h"
#include "MaterialFactory.h"
#include "ShaderFactory.h"
#include "MeshFactory.h"
#include "GUIRenderPass.h"

Application::Application(const ApplicationOptions& options)
{
	assert(options.window != nullptr && "Window must be valid");

	_timer = make_unique<Core::Timer>();

	_device = new Core::Device(*options.window);
}

bool Application::Prepare()
{
	_renderContext = new Core::RenderContext(*_device);

	auto swapChainExtent = _renderContext->GetSurfaceExtent();
	auto& swapChain = _renderContext->GetSwapChain();

	_scene = new SampleScene(*_device, (float)swapChainExtent.width, (float)swapChainExtent.height);

	_renderPipeline = new Core::ForwardRenderPipeline(*_device, *_scene, swapChain);
	_renderPipeline->Prepare();

	_guiRenderPass = new GUIRenderPass(*_device, swapChain, _renderPipeline->GetColorRenderTarget());
	_guiRenderPass->Prepare();

	return true;
}

void Application::Update()
{
	_guiRenderPass->Update();
	
	auto deltaTime = static_cast<float>(_timer->tick<Core::Timer::Seconds>());

	auto components = _scene->GetComponents<Core::Component>();
	for (auto component : components)
	{
		component->UpdateFrame(deltaTime);
	}

	//todo : update scene
	//todo : update stats
}

Application::~Application()
{
	delete(_guiRenderPass);

	Core::ShaderFactory::DeleteCache();
	Core::MaterialFactory::DeleteCache();
	Core::MeshFactory::DeleteCache();

	delete(_renderPipeline);
	delete(_scene);
	delete(_renderContext);
	delete(_device);

	Core::Window::Instance().Delete();
}

void Application::Draw()
{
	auto& commandBuffer = _renderContext->Begin();

	_renderPipeline->Draw(commandBuffer,
		_renderContext->GetCurrentFrame(), 
		_renderContext->GetImageIndex());

	_guiRenderPass->Draw(commandBuffer, 
		_renderContext->GetCurrentFrame(),
		_renderContext->GetImageIndex());

	commandBuffer.EndCommandBuffer();

	_renderContext->Submit(commandBuffer);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(_device->GetDevice());
}
