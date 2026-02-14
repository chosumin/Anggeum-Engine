#pragma once

namespace Core
{
	class Device;
	class RenderPass;
	class SwapChain;
	class Texture;
	class Image;

	class Framebuffer
	{
	public:
		Framebuffer(Device& device, RenderPass& renderPass, const vector<Texture*>& attachments);
		~Framebuffer();

		VkFramebuffer GetHandle() const { return _framebuffer; }
		VkExtent2D GetExtent() const { return _extent; }

	private:
		Device& _device;
		VkFramebuffer _framebuffer = VK_NULL_HANDLE;
		VkExtent2D _extent;
	};
}

