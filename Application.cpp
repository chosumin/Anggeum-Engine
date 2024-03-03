#include "stdafx.h"
#include "Application.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Mesh.h"
#include "Texture.h"
#include "timer.h"
#include "SampleScene.h"
#include "Component.h"
#include "PerspectiveCamera.h"
#include "SampleRenderPass.h"
#include "Framebuffer.h"
#include "SampleShader.h"

Application::Application()
{
	_timer = make_unique<Core::Timer>();

	Core::Window::Instance().Initialize(2000, 1000, "Hello Vulkan", this);
	Core::Device::Instance().Initialize(Core::Window::Instance());

	auto& device = Core::Device::Instance();

	_swapChain = new Core::SwapChain();
	auto swapChainExtent = _swapChain->GetSwapChainExtent();
	_renderPass = new Core::SampleRenderPass(device, swapChainExtent, _swapChain->GetImageFormat());
	_framebuffer = new Core::Framebuffer(device, *_swapChain, *_renderPass);

	_commandBuffer = new Core::CommandBuffer();
	
	_scene = new SampleScene((float)swapChainExtent.width, (float)swapChainExtent.height, _commandBuffer->GetCommandPool());

	_shader = new Core::SampleShader(device, *_commandBuffer);

	_pipeline = new Core::Pipeline(device,
		*_renderPass, *_shader);
}

bool Application::Prepare(const ApplicationOptions& options)
{
	assert(options.window != nullptr && "Window must be valid");

	/*auto& debugInfo = GetDebugInfo();

	debugInfo.insert<field::MinMax, float>("fps", _fps);
	debugInfo.insert<field::MinMax, float>("frame_time", _frameTime);*/

	_lockSimulationSpeed = options.BenchmarkEnabled;
	_window = options.window;

	return false;
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
	delete(_pipeline);
	delete(_shader);
	delete(_scene);
	delete(_commandBuffer);
	delete(_framebuffer);
	delete(_renderPass);
	delete(_swapChain);

	Core::Device::Instance().Delete();
	Core::Window::Instance().Delete();
}

void Application::DrawFrame()
{
	auto cameras = _scene->GetComponents<Core::PerspectiveCamera>();

	uint32_t currentFrame = _commandBuffer->GetCurrentFrame();

	for (auto camera : cameras)
	{
		_shader->SetBuffer(currentFrame, 0, &camera->Matrices);
	}

	_commandBuffer->RecordCommandBuffer(*_swapChain);
	_commandBuffer->BeginCommandBuffer();

	auto framebuffer = _framebuffer->GetHandle(_commandBuffer->GetImageIndex());
	auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(framebuffer, _swapChain->GetSwapChainExtent());
	_commandBuffer->BeginRenderPass(renderPassBeginInfo);
	_commandBuffer->BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
		_pipeline->GetGraphicsPipeline());

	VkViewport viewport;
	VkRect2D scissor;
	_swapChain->GetViewportAndScissor(viewport, scissor);
	_commandBuffer->SetViewportAndScissor(viewport, scissor);

	auto descriptorLayout = _shader->GetPipelineLayout();
	auto descriptorSet = _shader->GetDescriptorSet(currentFrame);
	_commandBuffer->BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
		descriptorLayout, descriptorSet);

	{
		auto meshes = _scene->GetComponents<Core::Mesh>();

		for (auto mesh : meshes)
		{
			mesh->DrawFrame(*_commandBuffer, *_shader);
			_commandBuffer->Flush(*_shader);
			_commandBuffer->BindVertexBuffers(mesh->GetVertexBuffer());
			_commandBuffer->BindIndexBuffer(mesh->GetIndexBuffer(), VK_INDEX_TYPE_UINT32);
			_commandBuffer->DrawIndexed(mesh->GetIndexCount(), 1);
		}
	}
	_commandBuffer->EndFrame(*_swapChain);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(Core::Device::Instance().GetDevice());
}
