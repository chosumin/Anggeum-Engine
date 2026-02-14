#include "stdafx.h"
#include "Framebuffer.h"
#include "RenderPass.h"
#include "Texture.h"
#include "Image.h"

Core::Framebuffer::Framebuffer(Device& device, RenderPass& renderPass, const vector<Texture*>& attachments)
	: _device(device)
{
	if (attachments.empty())
		throw std::runtime_error("Framebuffer requires at least one attachment!");

	auto firstExtent = attachments[0]->GetExtent();
	_extent = { firstExtent.width, firstExtent.height };

	vector<VkImageView> imageViews;
	imageViews.reserve(attachments.size());

	for (auto* attachment : attachments)
	{
		imageViews.push_back(attachment->GetImageView());
	}

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = renderPass.GetHandle();
	framebufferInfo.attachmentCount = static_cast<uint32_t>(imageViews.size());
	framebufferInfo.pAttachments = imageViews.data();
	framebufferInfo.width = _extent.width;
	framebufferInfo.height = _extent.height;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(_device.GetDevice(), &framebufferInfo, nullptr, &_framebuffer) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create framebuffer!");
	}
}

Core::Framebuffer::~Framebuffer()
{
	if (_framebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(_device.GetDevice(), _framebuffer, nullptr);
	}
}
