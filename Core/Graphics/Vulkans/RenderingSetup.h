#pragma once

namespace Core
{
	class Texture;

	struct RenderingSetup
	{
		bool valid = false;

		VkExtent2D renderArea{};
		uint32_t layerCount = 1;

		vector<VkRenderingAttachmentInfo> colorAttachments;
		VkRenderingAttachmentInfo depthAttachment{};
		bool hasDepth = false;

		// Typed helpers: fill the view/layout/sType boilerplate from the texture
		// and take the render area from its extent.
		void AddColorAttachment(Texture& texture, VkAttachmentLoadOp loadOp,
			VkAttachmentStoreOp storeOp, VkClearValue clear,
			Texture* resolveTarget = nullptr);
		void SetDepthAttachment(Texture& texture, VkAttachmentLoadOp loadOp,
			VkAttachmentStoreOp storeOp, VkClearValue clear);

		// Rebuilds the internal VkRenderingInfo's attachment pointers; cheap and
		// idempotent. The reference is valid as long as this object is.
		const VkRenderingInfo& Finalize() const;

	private:
		mutable VkRenderingInfo _renderingInfo{};
	};
}
