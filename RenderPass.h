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
		VkImageUsageFlags UsageFlags;
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

		VkRenderPassBeginInfo CreateRenderPassBeginInfo(VkFramebuffer framebuffer, VkExtent2D swapChainExtent);
		vector<VkImageView> GetAttachments(VkImageView swapChainImageView) const;
		void Cleanup();
		void Resize(SwapChain& swapChain);

		virtual void Prepare() = 0;
		virtual void Draw() = 0;
	protected:
		void CreateRenderTarget(VkExtent2D extent, VkFormat format, VkImageLayout layout, VkImageUsageFlags usageFlags);
		void CreateDepthRenderTarget(VkExtent2D extent);
		void CreateColorRenderTarget(VkExtent2D extent, VkFormat format);
		void CreateRenderPass();
	private:
		VkSampleCountFlagBits GetMaxUsableSampleCount();
	protected:
		Device& _device;
	private:
		VkRenderPass _renderPass;

		vector<unique_ptr<RenderTarget>> _renderTargets;
		unique_ptr<RenderTarget> _depth;
		unique_ptr<RenderTarget> _color;

		VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;

		array<VkClearValue, 3> _clearValues{};
	};
}

