#pragma once

namespace Core
{
	class Window;
	class Pipeline;
	class SwapChain;
	class CommandBuffer;
	class UniformBuffer;
	class Texture;
	class Polygon;
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

	Core::Polygon* _polygon;

	Core::UniformBuffer* _mvpBuffer;
	Core::Texture* _texture;
};

