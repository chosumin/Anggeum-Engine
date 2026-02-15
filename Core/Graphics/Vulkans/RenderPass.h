#pragma once
#include "PipelineState.h"
#include "Graphics/Vulkans/Texture.h"

namespace Core
{
	struct Attachment
	{
	public:
		VkFormat Format;
		VkSampleCountFlagBits Samples;
		VkAttachmentLoadOp LoadOp;
		VkAttachmentStoreOp StoreOp;
		VkImageLayout FinalLayout;
	};

	class SwapChain;
	class Framebuffer;
	class RenderPass
	{
	public:
		RenderPass(Device& device);
		virtual ~RenderPass();

		VkRenderPass GetHandle() const { return _renderPass; }

		VkRenderPassBeginInfo CreateRenderPassBeginInfo(
			Framebuffer& framebuffer);

		void CreateAttachment(Texture* renderTarget,
			VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateDepthAttachment(Texture* renderTarget, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateDepthAttachment(VkFormat format, VkSampleCountFlagBits samples,
			VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		void CreateColorAttachment(Texture* renderTarget, 
			VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp, 
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		void CreateColorAttachment(VkFormat format, VkSampleCountFlagBits samples,
			VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		void CreateColorResolveAttachment();
		void CreateRenderPass();
	private:
		Device& _device;
		VkRenderPass _renderPass;

		vector<unique_ptr<Attachment>> _inputAttachments;
		unique_ptr<Attachment> _depth;
		unique_ptr<Attachment> _color;
		unique_ptr<Attachment> _colorResolve;

		vector<VkClearValue> _clearValues{};
	};
}

