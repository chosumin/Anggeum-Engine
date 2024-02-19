#pragma once

#include "InputEvents.h"

namespace Core
{
	class Window;
	class Pipeline;
	class SwapChain;
	class CommandBuffer;
	class Texture;
	class Mesh;
	class Timer;
	class Window;
	class Scene;
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

	bool Prepare(const ApplicationOptions& options);
	void Update();
	void Finish();
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
	//Core::Camera* _camera;
	Core::Pipeline* _pipeline;
	Core::SwapChain* _swapChain;
	Core::CommandBuffer* _commandBuffer;

	Core::Scene* _scene;
	Core::Mesh* _Mesh;
};
