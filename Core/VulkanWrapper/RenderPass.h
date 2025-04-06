#pragma once
#include "PipelineState.h"
#include "VulkanWrapper/Texture.h"

namespace Core
{
	struct Attachment
	{
	public:
		Texture* RenderTarget;
		VkAttachmentLoadOp LoadOp;
		VkAttachmentStoreOp StoreOp;
	};

	class SwapChain;
	class CommandBuffer;
	class Framebuffer;
	class PipelineState;
	class RenderPass
	{
	public:
		RenderPass(Device& device);
		virtual ~RenderPass();

		VkRenderPass GetHandle() const { return _renderPass; }

		VkRenderPassBeginInfo CreateRenderPassBeginInfo(uint32_t imageIndex);
		vector<VkImageView> GetAttachments(VkImageView swapChainImageView) const;

		virtual void Prepare() = 0;
		virtual void Draw(CommandBuffer& commandBuffer, 
			uint32_t currentFrame, uint32_t imageIndex) = 0;
	protected:
		void CreateAttachment(Texture* renderTarget,
			VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateDepthAttachment(Texture* renderTarget, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateColorAttachment(Texture* renderTarget, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateColorResolveAttachment();
		void CreateRenderPass();
		void CreateFrameBuffer(SwapChain& swapChain);

		VkExtent2D GetBufferExtent2D();
	protected:
		Device& _device;
		Framebuffer* _framebuffer;
		VkRenderPass _renderPass;

		PipelineState* _pipelineState;
	private:
		vector<unique_ptr<Attachment>> _inputAttachments;
		unique_ptr<Attachment> _depth;
		unique_ptr<Attachment> _color;
		unique_ptr<Attachment> _colorResolve;

		vector<VkClearValue> _clearValues{};
	};
}

