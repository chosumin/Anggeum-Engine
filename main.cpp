#include "stdafx.h"
#include "Application.h"

int main()
{
	Application application;

	while (Core::Window::Instance().IsClosed() == false)
	{
		glfwPollEvents();
		application.DrawFrame();
	}

	application.WaitIdle();
}