#pragma once

namespace Core
{
	class Device;
	class RenderPass;
	class SwapChain;
	class Image;
	class Framebuffer
	{
	public:
		Framebuffer(Device& device, SwapChain& swapChain, RenderPass& renderPass);
		Framebuffer(Device& device, RenderPass& renderPass, Image& image);
		virtual ~Framebuffer();

		VkFramebuffer GetHandle(size_t imageIndex) const 
		{
			size_t index = std::min(imageIndex, _framebuffers.size() - 1);

			return _framebuffers[index];
		}

		VkExtent2D GetExtent() const
		{
			return _extent;
		}

		void Cleanup();
		void Resize(SwapChain& swapChain);
	private:
		void CreateFramebuffers(SwapChain& swapChain);
	private:
		Device& _device;
		RenderPass& _renderPass;
		VkExtent2D _extent;
		vector<VkFramebuffer> _framebuffers;

		bool _isSwapChainFramebuffer = false;
	};
}

