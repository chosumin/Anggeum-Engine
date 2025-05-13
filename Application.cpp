#include "stdafx.h"
#include "Application.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"
#include "Graphics/TransferContext.h"
#include "Sample/SampleScene.h"
#include "Sample/ForwardRenderPipeline.h"
#include "Sample/RendererPasses/GUIRenderPass.h"
#include "Utils/timer.h"

Application::Application(const ApplicationOptions& options)
{
	assert(options.window != nullptr && "Window must be valid");

	_timer = make_unique<Core::Timer>();

	_device = new Core::Device(*options.window);
	_workerThreadManager = new Core::WorkerThreadManager(*_device);
	_transferContext = new Core::TransferContext(*_device, *_workerThreadManager);
	_renderContext = new Core::RenderContext(*_device);
}

bool Application::Prepare()
{
	auto swapChainExtent = _renderContext->GetSurfaceExtent();
	auto& swapChain = _renderContext->GetSwapChain();

	_scene = new SampleScene(*_device, (float)swapChainExtent.width, (float)swapChainExtent.height, _transferContext);

	_transferContext->Wait();

	_renderPipeline = new Core::ForwardRenderPipeline(*_device, *_workerThreadManager,
		*_scene, swapChain);
	_renderPipeline->Prepare();

	_guiRenderPass = new GUIRenderPass(*_device, *_workerThreadManager, swapChain, _renderPipeline->GetColorRenderTarget());
	_guiRenderPass->Prepare();

	return true;
}

Application::~Application()
{
	delete(_guiRenderPass);

	delete(_renderPipeline);
	delete(_scene);

	delete(_renderContext);
	delete(_transferContext);
	delete(_workerThreadManager);
	delete(_device);

	Core::Window::Instance().Delete();
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

	_scene->Update();

	//todo : update stats
}

void Application::Draw()
{
	_transferContext->UpdateFrame(_renderContext->GetCurrentFrame());
	_transferContext->Wait();

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
