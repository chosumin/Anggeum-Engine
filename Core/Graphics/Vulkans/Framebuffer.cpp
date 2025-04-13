#include "stdafx.h"
#include "Framebuffer.h"
#include "RenderPass.h"
#include "SwapChain.h"
#include "Graphics/RenderContext.h"
#include "Image.h"

Core::Framebuffer::Framebuffer(Device& device, SwapChain& swapChain, RenderPass& renderPass)
	:_device{device}, _renderPass(renderPass), _isSwapChainFramebuffer(true)
{
    _extent = swapChain.GetSwapChainExtent();
    CreateFramebuffers(swapChain);

    auto a = std::bind(&Framebuffer::Resize, this, std::placeholders::_1);
    Core::RenderContext::AddResizeCallback(a);
}

Core::Framebuffer::Framebuffer(Device& device, RenderPass& renderPass, Image& image)
    :_device{ device }, _renderPass(renderPass), _isSwapChainFramebuffer(false)
{
    auto extent = image.GetExtent();
	_extent = { extent.width, extent.height };

    _framebuffers.resize(1);

    for (size_t i = 0; i < _framebuffers.size(); i++)
    {
        auto attachments =
            _renderPass.GetAttachments(VkImageView{});

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = _renderPass.GetHandle();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = _extent.width;
        framebufferInfo.height = _extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(_device.GetDevice(), &framebufferInfo, nullptr, &_framebuffers[i]) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

Core::Framebuffer::~Framebuffer()
{
    Cleanup();

	if (_isSwapChainFramebuffer)
	{
		auto a = std::bind(&Framebuffer::Resize, this, std::placeholders::_1);
		Core::RenderContext::RemoveResizeCallback(a);
	}
}

void Core::Framebuffer::Cleanup()
{
    for (auto framebuffer : _framebuffers)
        vkDestroyFramebuffer(_device.GetDevice(), framebuffer, nullptr);
}

void Core::Framebuffer::Resize(SwapChain& swapChain)
{
    Cleanup();

    _extent = swapChain.GetSwapChainExtent();
    CreateFramebuffers(swapChain);
}

void Core::Framebuffer::CreateFramebuffers(SwapChain& swapChain)
{
    _framebuffers.resize(swapChain.GetSwapChainCount());

    for (size_t i = 0; i < _framebuffers.size(); i++)
    {
        auto attachments = 
            _renderPass.GetAttachments(swapChain.GetImageView(i));

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = _renderPass.GetHandle();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = _extent.width;
        framebufferInfo.height = _extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(_device.GetDevice(), &framebufferInfo, nullptr, &_framebuffers[i]) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}
