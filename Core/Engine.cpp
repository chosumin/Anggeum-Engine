#include "stdafx.h"
#include "Engine.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/RenderContext.h"
#include "Graphics/SyncContext.h"
#include "Graphics/TransferContext.h"
#include "Graphics/RenderScene.h"
#include "Graphics/ResourceManager.h"
#include "Foundation/Scene.h"
#include "Graphics/ForwardRenderPipeline.h"
#include "Sample/SampleScene.h"
#include "Utils/timer.h"
#include "Utils/CrashHandler.h"

Core::Engine::Engine(const EngineOptions& options)
{
    assert(options.window != nullptr && "Window must be valid");

    Core::InstallCrashHandler();

    _timer = make_unique<Core::Timer>();

    _device = new Core::Device(*options.window);
    
    _syncContext = new Core::SyncContext(*_device);

    _resourceManager = new Core::ResourceManager(*_device, *_syncContext);
    _workerThreadManager = new Core::WorkerThreadManager(*_device, *_syncContext);
    _transferContext = new Core::TransferContext(*_device, *_workerThreadManager,
        *_syncContext);

    auto* sampleScene = new SampleScene(*_device, *_resourceManager);
    _scene = sampleScene;
    _renderScene = new Core::RenderScene(*_device, *_resourceManager, *_scene,
        *_syncContext, *_transferContext);
    _renderContext = new Core::RenderContext(*_device, *_resourceManager, *_renderScene, *_syncContext);
    _status = make_unique<Core::Status>(*_renderContext);

    _resourceManager->Prepare(*_renderContext, _renderScene->GetAssetStreamer());

    auto swapChainExtent = _renderContext->GetSurfaceExtent();
    auto& swapChain = _renderContext->GetSwapChain();

    sampleScene->Load((float)swapChainExtent.width, (float)swapChainExtent.height, _renderContext);

    _renderPipeline = new Core::ForwardRenderPipeline(*_device, *_resourceManager, *_workerThreadManager,
        *_renderScene, swapChain, *_syncContext);
}

Core::Engine::~Engine()
{
    // Reverse of construction: the context's frames reference the render scene's
    // batch, and the render scene references the scene.
    delete(_renderPipeline);
    delete(_renderContext);
    delete(_renderScene);
    delete(_scene);
    delete(_transferContext);
    delete(_workerThreadManager);
    delete(_resourceManager);
    delete(_syncContext);
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

	// After Begin on purpose: the upload phases stage frame-slot memory,
	// which requires this slot's in-flight wait in Begin.
	{
		ScopedCpuTimer timer(phases.transferWaitMs);

		// One completed-value poll per frame; pools recycle off this cache.
		_syncContext->RefreshCompletedCache();

		// Destroy retired resources whose timelines the GPU has passed.
		_resourceManager->DestroyRetired();

		_transferContext->BeginFrame();

		// Streams, syncs the GPU mirrors and hands this frame's upload jobs
		// to the transfer context.
		_renderScene->SyncManagers(*_scene, extents,
			_transferContext->TakePromotedCount());

		// Waits only for the must-land recordings (memcpys); IO-bound loads
		// keep cooking.
		_transferContext->Flush(/*waitForRecordings*/ true);
	}

	{
		ScopedCpuTimer timer(phases.guiMs);
		_renderPipeline->OnGUI(_renderContext->GetCurrentFrame());
		_status->OnGUI();
		_transferContext->OnGUI();
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

	FrameCounter::IncreaseFrame();
}

void Core::Engine::WaitIdle()
{
    vkDeviceWaitIdle(_device->GetDevice());
}
