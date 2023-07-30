//https://openmynotepad.tistory.com/81 프로젝트 설정
//https://blog.naver.com/dmatrix/221809475599
//https://docs.google.com/document/d/1SWICNMyYS0egmK_vxbDvWTg3-vRuB9BdEQzlVJEK7rc/edit#heading=h.p894kr3lcuq1
//https://github.com/SaschaWillems/Vulkan
//https://vulkan-tutorial.com/

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