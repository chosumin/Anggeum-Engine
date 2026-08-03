#include "stdafx.h"
#include "RenderingSetup.h"
#include "Texture.h"

using namespace Core;

void RenderingSetup::AddColorAttachment(Texture& texture, VkAttachmentLoadOp loadOp,
	VkAttachmentStoreOp storeOp, VkClearValue clear, Texture* resolveTarget)
{
	VkRenderingAttachmentInfo info{};
	info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	info.imageView = texture.GetImageView();
	info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	info.loadOp = loadOp;
	info.storeOp = storeOp;
	info.clearValue = clear;

	if (resolveTarget != nullptr)
	{
		info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		info.resolveImageView = resolveTarget->GetImageView();
		info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	colorAttachments.push_back(info);

	const auto& extent = texture.GetExtent();
	renderArea = { extent.width, extent.height };
}

void RenderingSetup::SetDepthAttachment(Texture& texture, VkAttachmentLoadOp loadOp,
	VkAttachmentStoreOp storeOp, VkClearValue clear)
{
	depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = texture.GetImageView();
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = loadOp;
	depthAttachment.storeOp = storeOp;
	depthAttachment.clearValue = clear;
	hasDepth = true;

	const auto& extent = texture.GetExtent();
	renderArea = { extent.width, extent.height };
}

const VkRenderingInfo& RenderingSetup::Finalize() const
{
	_renderingInfo = {};
	_renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	_renderingInfo.renderArea = { { 0, 0 }, renderArea };
	_renderingInfo.layerCount = layerCount;
	_renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
	_renderingInfo.pColorAttachments = colorAttachments.data();
	_renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;
	return _renderingInfo;
}
