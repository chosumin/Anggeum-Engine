#pragma once

namespace Core
{
	class Device;
	class RenderPass;
	class SwapChain;
	class Framebuffer
	{
	public:
		Framebuffer(Device& device, SwapChain& swapChain, RenderPass& renderPass);
		virtual ~Framebuffer();

		VkFramebuffer GetHandle(size_t imageIndex) const 
		{
			return _framebuffers[imageIndex];
		}

		void Cleanup();
		void Resize(SwapChain& swapChain);
	private:
		void CreateFramebuffers(SwapChain& swapChain);
	private:
		Device& _device;
		RenderPass& _renderPass;
		vector<VkFramebuffer> _framebuffers;
	};
}

