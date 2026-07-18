#include "stdafx.h"
#include "Engine.h"

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	Core::Window::Instance().Initialize(1920, 1080, "Anggeum Engine");

	Core::EngineOptions options{ false, &Core::Window::Instance() };
	Core::Engine engine(options);

	while (Core::Window::Instance().IsClosed() == false)
	{
		glfwPollEvents();
		engine.Update();
		engine.Draw();
	}

	engine.WaitIdle();

	return 0;
}