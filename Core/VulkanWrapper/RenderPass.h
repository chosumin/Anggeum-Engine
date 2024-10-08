#pragma once
#include "RenderTarget.h"
#include "PipelineState.h"

namespace Core
{
	struct Attachment
	{
	public:
		RenderTarget* RenderTarget;
		VkAttachmentLoadOp LoadOp;
		VkAttachmentStoreOp StoreOp;
	};

	class Device;
	class SwapChain;
	class CommandBuffer;
	class Framebuffer;
	class CommandPool;
	class PipelineState;
	class RenderPass
	{
	public:
		RenderPass(Device& device);
		virtual ~RenderPass();

		VkRenderPass GetHandle() const { return _renderPass; }

		VkRenderPassBeginInfo CreateRenderPassBeginInfo(VkFramebuffer framebuffer, VkExtent2D swapChainExtent);
		vector<VkImageView> GetAttachments(VkImageView swapChainImageView) const;

		virtual void Prepare() = 0;
		virtual void Draw(CommandBuffer& commandBuffer, 
			uint32_t currentFrame, uint32_t imageIndex) = 0;
	protected:
		void CreateAttachment(RenderTarget* renderTarget, 
			VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateDepthAttachment(RenderTarget* renderTarget, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateColorAttachment(RenderTarget* renderTarget, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
		void CreateColorResolveAttachment();
		void CreateRenderPass();
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

