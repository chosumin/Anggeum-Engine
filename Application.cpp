#include "stdafx.h"
#include "Application.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"
#include "Graphics/TransferContext.h"
#include "Graphics/ResourceCache.h"
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
	_renderContext->Prepare();

	// Initialize ResourceCache with RenderContext for bindless support
	auto& resourceCache = _device->GetResourceCache();
	resourceCache.Initialize(*_renderContext);

	auto swapChainExtent = _renderContext->GetSurfaceExtent();
	auto& swapChain = _renderContext->GetSwapChain();

	_scene = new SampleScene(*_device, (float)swapChainExtent.width, (float)swapChainExtent.height, _transferContext, _renderContext);

	_transferContext->Wait();

	_renderPipeline = new Core::ForwardRenderPipeline(*_device, *_workerThreadManager,
		*_scene, swapChain);
	_renderPipeline->Prepare();

	_renderPipeline->RegisterGiTexturesToBindless(*_renderContext);

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
	_transferContext->UpdateFrame(_renderContext->GetCurrentFrameIndex());
	_transferContext->Wait();

	_renderContext->Begin();

	uint32_t imageIndex = _renderContext->GetImageIndex();

	_renderPipeline->Draw(_renderContext->GetCurrentFrame(), imageIndex);

	_guiRenderPass->Draw(_renderContext->GetCurrentFrame(), imageIndex);

	_renderContext->Submit();
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(_device->GetDevice());
}