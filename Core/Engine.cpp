#include "stdafx.h"
#include "Engine.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/RenderContext.h"
#include "Graphics/TransferContext.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/ForwardRenderPipeline.h"
#include "Sample/SampleScene.h"
#include "Utils/timer.h"

Core::Engine::Engine(const EngineOptions& options)
{
    assert(options.window != nullptr && "Window must be valid");

    _timer = make_unique<Core::Timer>();

    _device = new Core::Device(*options.window);
    _workerThreadManager = new Core::WorkerThreadManager(*_device);
    _transferContext = new Core::TransferContext(*_device, *_workerThreadManager);
    _renderContext = new Core::RenderContext(*_device);

    auto& resourceCache = _device->GetResourceCache();
    resourceCache.Prepare(*_renderContext);

    auto swapChainExtent = _renderContext->GetSurfaceExtent();
    auto& swapChain = _renderContext->GetSwapChain();

    _scene = new SampleScene(*_device, (float)swapChainExtent.width, (float)swapChainExtent.height, _transferContext, _renderContext);

    _transferContext->Wait();

    _renderPipeline = new Core::ForwardRenderPipeline(*_device, *_workerThreadManager,
        *_scene, swapChain);
}

Core::Engine::~Engine()
{
    delete(_renderPipeline);
    delete(_scene);
    delete(_renderContext);
    delete(_transferContext);
    delete(_workerThreadManager);
    delete(_device);

    Core::Window::Instance().Delete();
}

void Core::Engine::Update()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    auto deltaTime = static_cast<float>(_timer->tick<Core::Timer::Seconds>());

    auto components = _scene->GetComponents<Core::Component>();
    for (auto component : components)
    {
        component->UpdateFrame(deltaTime);
    }

    _scene->Update();
}

void Core::Engine::Draw()
{
    _transferContext->UpdateFrame(_renderContext->GetCurrentFrameIndex());
    _transferContext->Wait();

    _renderContext->Begin();

	_renderPipeline->OnGUI(_renderContext->GetCurrentFrame());

    uint32_t imageIndex = _renderContext->GetImageIndex();
    _renderPipeline->Draw(_renderContext->GetCurrentFrame(), imageIndex);

    _renderContext->Submit();
}

void Core::Engine::WaitIdle()
{
    vkDeviceWaitIdle(_device->GetDevice());
}
