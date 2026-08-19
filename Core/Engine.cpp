#include "stdafx.h"
#include "Engine.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/RenderContext.h"
#include "Graphics/TransferContext.h"
#include "Graphics/UploadScheduler.h"
#include "Graphics/RenderScene.h"
#include "Graphics/ResourceManager.h"
#include "Foundation/Scene.h"
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
    _uploadScheduler = new Core::UploadScheduler(*_device, *_transferContext);

    auto* sampleScene = new SampleScene(*_device);
    _scene = sampleScene;
    _renderScene = new Core::RenderScene(*_device, *_scene, *_uploadScheduler);
    _renderContext = new Core::RenderContext(*_device, *_renderScene);
    _status = make_unique<Core::Status>(*_renderContext);

    auto& resourceManager = _device->GetResourceManager();
    resourceManager.Prepare(*_renderContext);

    auto swapChainExtent = _renderContext->GetSurfaceExtent();
    auto& swapChain = _renderContext->GetSwapChain();

    sampleScene->Load((float)swapChainExtent.width, (float)swapChainExtent.height, _renderContext);

    _renderPipeline = new Core::ForwardRenderPipeline(*_device, *_workerThreadManager,
        *_renderScene, swapChain);
}

Core::Engine::~Engine()
{
    // Reverse of construction: the context's frames reference the render scene's
    // batch, and the render scene references the scene.
    delete(_renderPipeline);
    delete(_renderContext);
    delete(_renderScene);
    delete(_scene);
    delete(_uploadScheduler);
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
	auto& phases = _status->GetCpuPhases();

	auto extents = _renderContext->GetSurfaceExtent();

	{
		ScopedCpuTimer timer(phases.beginMs);
		_renderContext->Begin(*_scene, extents);
	}

	// After Begin on purpose: Sync now also drives the terrain update, whose
	// frame-slot staging writes require this slot's in-flight wait in Begin.
	{
		ScopedCpuTimer timer(phases.transferWaitMs);
		_transferContext->UpdateFrame(_renderContext->GetCurrentFrameIndex());

		// Sync drives the upload scheduler internally (submit + flush) before it
		// writes descriptors and rebuilds the draw set, so no separate Wait is
		// needed here. Each manager self-gates and clears its own dirty state.
		_renderScene->Sync(*_scene, extents);
	}

	{
		ScopedCpuTimer timer(phases.guiMs);
		_renderPipeline->OnGUI(_renderContext->GetCurrentFrame());
		_status->OnGUI();
	}

	uint32_t imageIndex = _renderContext->GetImageIndex();
	{
		ScopedCpuTimer timer(phases.recordMs);
		_renderPipeline->Draw(*_renderContext, _renderContext->GetCurrentFrame(), imageIndex);
	}

	{
		ScopedCpuTimer timer(phases.submitMs);
		_renderContext->Submit();
	}
}

void Core::Engine::WaitIdle()
{
    vkDeviceWaitIdle(_device->GetDevice());
}
