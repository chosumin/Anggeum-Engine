//https://docs.google.com/document/d/1SWICNMyYS0egmK_vxbDvWTg3-vRuB9BdEQzlVJEK7rc/edit#heading=h.p894kr3lcuq1
//https://www.vulkan.org/learn#vulkan-tutorials

//https://www.khronos.org/opengl/wiki/Layout_Qualifier_(GLSL)
//https://fgiesen.wordpress.com/2011/07/02/a-trip-through-the-graphics-pipeline-2011-part-2/

#include "stdafx.h"
#include "Application.h"

int main()
{
	Application application;

	while (Core::Window::Instance().IsClosed() == false)
	{
		glfwPollEvents();
		application.Update();
		application.DrawFrame();
	}

	application.WaitIdle();

	return 0;
}