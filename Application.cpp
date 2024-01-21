#include "stdafx.h"
#include "Application.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Polygon.h"
#include "Texture.h"
#include "Camera.h"
#include "timer.h"
#include "MVPUniformBuffer.h"

Application::Application()
{
	_timer = make_unique<Core::Timer>();

	Core::Window::Instance().Initialize(2000, 1000, "Hello Vulkan", this);
	Core::Device::Instance().Initialize(Core::Window::Instance());

	_swapChain = new Core::SwapChain();

	auto swapCahinExtent = _swapChain->GetSwapChainExtent();
	_camera = new Core::Camera((float)swapCahinExtent.width, (float)swapCahinExtent.height);

	_commandBuffer = new Core::CommandBuffer();

	_texture = new Core::Texture(_commandBuffer->GetCommandPool(),
		"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha);

	_buffer = new Core::ModelUniformBuffer();
	_polygon = new Core::Polygon(_commandBuffer->GetCommandPool(), vec3(), "Models/viking_room.obj", _buffer);
	_polygon2 = new Core::Polygon(_commandBuffer->GetCommandPool(), vec3(2.0f, 0.0f, 0.0f), "Models/viking_room.obj", _buffer);

	_pipeline = new Core::Pipeline(
		*_swapChain,
		"shaders/simple_vs.vert.spv", "shaders/simple_fs.frag.spv",
		{ _camera->GetUniformBuffer(), _buffer, _texture });
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

	_camera->UpdateFrame(deltaTime);

	//todo : update scene
	//todo : update gui
	//todo : update stats
}

void Application::Finish()
{
}

void Application::InputEvent(const Core::InputEvent& inputEvent)
{
	_camera->InputEvent(inputEvent);
}

Application::~Application()
{
	delete(_texture);
	delete(_polygon);
	delete(_polygon2);
	delete(_buffer);
	delete(_commandBuffer);
	delete(_pipeline);
	delete(_swapChain);

	delete(_camera);

	Core::Device::Instance().Delete();
	Core::Window::Instance().Delete();
}

//DebugInfo& Core::Application::GetDebugInfo()
//{
//	
//}

void Application::DrawFrame()
{
	_camera->GetUniformBuffer()->Update(_commandBuffer->GetCurrentFrame());

	_commandBuffer->RecordCommandBuffer(*_pipeline, *_swapChain);
	{
		_polygon->DrawFrame(_commandBuffer->GetCommandBuffer(), _commandBuffer->GetCurrentFrame());
		_polygon2->DrawFrame(_commandBuffer->GetCommandBuffer(), _commandBuffer->GetCurrentFrame());
	}
	_commandBuffer->EndFrame(*_pipeline, *_swapChain);
}

void Application::WaitIdle()
{
	vkDeviceWaitIdle(Core::Device::Instance().GetDevice());
}
