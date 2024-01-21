#pragma once

namespace Core
{
	class Window
	{
	public:
		static Core::Window& Instance()
		{
			static Core::Window instance;
			return instance;
		}
		static bool FramebufferResized;
	public:
		void Initialize(int width, int height, const string& windowName, void* application);
		void Delete();

		bool IsClosed();

		void CreateSurface(const VkInstance& instance, VkSurfaceKHR* surface);
		void GetWidthAndHeight(int& width, int& height);

		GLFWwindow* GetWindow()
		{
			return _window;
		}
	private:
		Window();
		~Window();
	private:
		static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
		{
			FramebufferResized = true;
		}
	private:
		GLFWwindow* _window{ nullptr };
		int _width;
		int _height;

		string _windowName;
	};
}