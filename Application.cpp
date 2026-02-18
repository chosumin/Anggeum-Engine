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
	resourceCache.Prepare(*_renderContext);

	auto swapChainExtent = _renderContext->GetSurfaceExtent();
	auto& swapChain = _renderContext->GetSwapChain();

	_scene = new SampleScene(*_device, (float)swapChainExtent.width, (float)swapChainExtent.height, _transferContext, _renderContext);

	_transferContext->Wait();

	_renderPipeline = new Core::ForwardRenderPipeline(*_device, *_workerThreadManager,
		*_scene, swapChain);
	_renderPipeline->Prepare();

	return true;
}

Application::~Application()
{
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
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	static float f = 0.0f;
	static int counter = 0;

	ImGui::Begin("Status");

	ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	ImGui::End();

	auto deltaTime = static_cast<float>(_timer->tick<Core::Timer::Seconds>());

	auto components = _scene->GetComponents<Core::Component>();
	for (auto component : components)
	{
		component->UpdateFrame(deltaTime);
	}

	_scene->Update();
}

void Application::Draw()
{
	_transferContext->UpdateFrame(_renderContext->GetCurrentFrameIndex());
	_transferContext->Wait();

	_renderContext->Begin();

	uint32_t imageIndex = _renderContext->GetImageIndex();

	_renderPipeline->Draw(_renderContext->GetCurrentFrame(), imageIndex);

	_renderContext->Submit();
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(_device->GetDevice());
}