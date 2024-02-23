#pragma once

namespace Core
{
	struct RenderTarget
	{
	public:
		VkFormat Format;
		VkImage Image;
		VkDeviceMemory ImageMemory;
		VkImageView ImageView;
		VkImageLayout Layout;
	};

	class Device;
	class SwapChain;
	class RenderPass
	{
	public:
		RenderPass(Device& device);
		virtual ~RenderPass();

		VkRenderPass GetHandle() const { return _renderPass; }
		VkSampleCountFlagBits GetMSAASamples() const { return _msaaSamples; }

		vector<VkImageView> GetAttachments() const;
		void Cleanup();
		void Resize(SwapChain& swapChain);
	protected:
		void CreateRenderTarget(VkExtent2D extent, VkFormat format, VkImageLayout layout);
		void CreateDepthRenderTarget(VkExtent2D extent);
		void CreateColorRenderTarget(VkExtent2D extent, VkFormat format);
		void CreateRenderPass();
	private:
		VkSampleCountFlagBits GetMaxUsableSampleCount();
	private:
		Device& _device;
		VkRenderPass _renderPass;

		vector<unique_ptr<RenderTarget>> _renderTargets;
		unique_ptr<RenderTarget> _depth;
		unique_ptr<RenderTarget> _color;

		VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;
	};
}

