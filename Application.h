#pragma once

#include "InputEvents.h"

namespace Core
{
	class Window;
	class Pipeline;
	class SwapChain;
	class CommandBuffer;
	class Texture;
	class Polygon;
	class Camera;
	class Timer;
	class Window;
	class ModelUniformBuffer;
}

struct ApplicationOptions
{
	bool BenchmarkEnabled{ false };
	Core::Window* window{ nullptr };
};

class Application
{
public:
	Application();
	~Application();

	virtual bool Prepare(const ApplicationOptions& options);
	virtual void Update();
	virtual void Finish();
	virtual void InputEvent(const Core::InputEvent& inputEvent);
	void DrawFrame();
	void WaitIdle();
protected:
	float _fps{ 0.0f };
	float _frameTime{ 0.0f };   // In ms
	uint32_t _frameCount{ 0 };
	uint32_t _lastFrameCount{ 0 };
	bool _lockSimulationSpeed{ false };
	Core::Window* _window{ nullptr };
private:
	//todo : instance
	//todo : device
	//todo : render context
	//todo : render pipeline
	//todo : scene
	//todo : gui
	//todo : stats

	unique_ptr<Core::Timer> _timer;
	Core::Camera* _camera;
	Core::Pipeline* _pipeline;
	Core::SwapChain* _swapChain;
	Core::CommandBuffer* _commandBuffer;

	Core::Polygon* _polygon;
	Core::Polygon* _polygon2;
	Core::ModelUniformBuffer* _buffer;
	Core::Texture* _texture;
};
