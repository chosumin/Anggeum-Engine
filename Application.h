#pragma once

#include "InputEvents.h"

namespace Core
{
	class Window;
	class Pipeline;
	class SwapChain;
	class CommandBuffer;
	class Timer;
	class Window;
	class Scene;
	class RenderContext;
	class IRenderPipeline;
	class ImGuiManager;
}

struct ApplicationOptions
{
	bool BenchmarkEnabled{ false };
	Core::Window* window{ nullptr };
};

class Application
{
public:
	Application(const ApplicationOptions& options);
	~Application();

	bool Prepare();
	void Update();
	void Draw();
	void WaitIdle();
protected:
	float _fps{ 0.0f };
	float _frameTime{ 0.0f };   // In ms
	uint32_t _frameCount{ 0 };
	uint32_t _lastFrameCount{ 0 };
	bool _lockSimulationSpeed{ false };
private:
	//todo : render pipeline
	//todo : stats

	unique_ptr<Core::Timer> _timer;
	Core::Device* _device;
	Core::IRenderPipeline* _renderPipeline;
	Core::RenderContext* _renderContext;
	Core::Scene* _scene;
	Core::ImGuiManager* _imgui;
};
