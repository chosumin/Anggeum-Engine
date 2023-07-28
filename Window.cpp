#include "stdafx.h"
#include "Window.h"

bool Core::Window::FramebufferResized = false;

Core::Window::Window()
	:_width(0), _height(0), _windowName("")
{
}

Core::Window::~Window()
{
}

void Core::Window::Initialize(int width, int height, const string& windowName)
{
	_width = width;
	_height = height;
	_windowName = windowName;

	InitWindow();
}

void Core::Window::Delete()
{
	glfwDestroyWindow(_window);
	glfwTerminate();
}

bool Core::Window::IsClosed()
{
	return glfwWindowShouldClose(_window);
}

void Core::Window::CreateSurface(const VkInstance& instance, VkSurfaceKHR* surface)
{
	if (glfwCreateWindowSurface(instance, _window, nullptr, surface) != VK_SUCCESS)
		throw std::runtime_error("failed to create window surface!");
}

void Core::Window::GetWidthAndHeight(int& width, int& height)
{
	width = _width;
	height = _height;
}

void Core::Window::InitWindow()
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	_window = glfwCreateWindow(_width, _height, _windowName.c_str(), nullptr, nullptr);
	glfwSetFramebufferSizeCallback(_window, Window::FramebufferResizeCallback);
}
