#include "stdafx.h"
#include "Application.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Mesh.h"
#include "Texture.h"
#include "timer.h"
#include "MVPUniformBuffer.h"
#include "SampleScene.h"
#include "Component.h"
#include "PerspectiveCamera.h"

Application::Application()
{
	_timer = make_unique<Core::Timer>();

	Core::Window::Instance().Initialize(2000, 1000, "Hello Vulkan", this);
	Core::Device::Instance().Initialize(Core::Window::Instance());

	_swapChain = new Core::SwapChain();

	auto swapCahinExtent = _swapChain->GetSwapChainExtent();

	_commandBuffer = new Core::CommandBuffer();

	_scene = new SampleScene((float)swapCahinExtent.width, (float)swapCahinExtent.height, _commandBuffer->GetCommandPool());

	auto cams = _scene->GetComponents<Core::PerspectiveCamera>();
	auto meshes = _scene->GetComponents<Core::Mesh>();

	vector<Core::IDescriptor*> descriptors{};
	for (auto cam : cams)
		descriptors.push_back(cam);
	for (auto mesh : meshes)
	{
		auto descs = mesh->GetDescriptors();
		descriptors.insert(descriptors.end(), descs.begin(), descs.end());
	}

	_pipeline = new Core::Pipeline(
		*_swapChain,
		"shaders/simple_vs.vert.spv", "shaders/simple_fs.frag.spv",
		descriptors);
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
	delete(_scene);
	delete(_commandBuffer);
	delete(_pipeline);
	delete(_swapChain);

	Core::Device::Instance().Delete();
	Core::Window::Instance().Delete();
}

//DebugInfo& Core::Application::GetDebugInfo()
//{
//	
//}

void Application::DrawFrame()
{
	auto cameras = _scene->GetComponents<Core::PerspectiveCamera>();

	for (auto camera : cameras)
	{
		camera->Update(_commandBuffer->GetCurrentFrame());
	}

	_commandBuffer->RecordCommandBuffer(*_pipeline, *_swapChain);
	{
		auto meshes = _scene->GetComponents<Core::Mesh>();

		for(auto mesh : meshes)
			mesh->DrawFrame(_commandBuffer->GetCommandBuffer(), _commandBuffer->GetCurrentFrame());
	}
	_commandBuffer->EndFrame(*_pipeline, *_swapChain);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(Core::Device::Instance().GetDevice());
}
