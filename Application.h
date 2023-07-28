#pragma once

namespace Core
{
	class Window;
	class Pipeline;
	class SwapChain;
	class CommandBuffer;
	class UniformBuffer;
}

class Application
{
public:
	Application();
	~Application();

	void DrawFrame();
	void WaitIdle();
private:
	Core::Pipeline* _pipeline;
	Core::SwapChain* _swapChain;
	Core::CommandBuffer* _commandBuffer;

	class Triangle* _triangle;
	Core::UniformBuffer* _mvpBuffer;
};

