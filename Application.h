#pragma once

#include "InputEvents.h"

namespace Core
{
	class Window;
	class SwapChain;
	class CommandBuffer;
	class Timer;
	class Window;
	class Scene;
	class RenderContext;
	class IRenderPipeline;
}

struct ApplicationOptions
{
	bool BenchmarkEnabled{ false };
	Core::Window* window{ nullptr };
};

class GUIRenderPass;

class Application
{
public:
	Application(const ApplicationOptions& options);
	~Application();

	bool Prepare();
	void Update();
	void Draw();
	void WaitIdle();
private:
	//todo : stats

	unique_ptr<Core::Timer> _timer;
	Core::Device* _device;
	Core::IRenderPipeline* _renderPipeline;
	Core::RenderContext* _renderContext;
	Core::Scene* _scene;
	GUIRenderPass* _guiRenderPass;
};
