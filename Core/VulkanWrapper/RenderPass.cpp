#include "stdafx.h"
#include "RenderPass.h"
#include "Utils/Utility.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "RenderContext.h"
#include "Framebuffer.h"

namespace Core
{
	RenderPass::RenderPass(Device& device)
        :_device{device}
	{
        _pipelineState = new PipelineState();
	}

	RenderPass::~RenderPass()
	{
        delete(_framebuffer);

        vkDestroyRenderPass(_device.GetDevice(), _renderPass, nullptr);
	}

    VkRenderPassBeginInfo RenderPass::CreateRenderPassBeginInfo(VkFramebuffer framebuffer, VkExtent2D swapChainExtent)
    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = _renderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChainExtent;

        VkClearValue clearValue{};

        if (_color != nullptr)
        {
            clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
            _clearValues.push_back(clearValue);
        }

        if (_colorResolve != nullptr)
        {
            clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
            _clearValues.push_back(clearValue);
        }

        if (_depth != nullptr)
        {
            clearValue.depthStencil = { 1.0f, 0 };
            _clearValues.emplace_back(clearValue);
        }

        for (auto& renderTarget : _inputAttachments)
        {
            clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
            _clearValues.emplace_back(clearValue);
        }

        renderPassInfo.clearValueCount = static_cast<uint32_t>(_clearValues.size());
        renderPassInfo.pClearValues = _clearValues.data();

        return renderPassInfo;
    }

    vector<VkImageView> RenderPass::GetAttachments(VkImageView swapChainImageView) const
    {
        vector<VkImageView> attachments;

        if (_color != nullptr)
        {
            attachments.push_back(_color->RenderTarget->ImageView);
        }

        if (_colorResolve != nullptr)
        {
            attachments.push_back(swapChainImageView);
        }

        if (_depth != nullptr)
        {
            attachments.push_back(_depth->RenderTarget->ImageView);
        }

        for (auto& renderTarget : _inputAttachments)
        {
            attachments.push_back(renderTarget->RenderTarget->ImageView);
        }

        return attachments;
    }

    void RenderPass::CreateAttachment(RenderTarget* renderTarget,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
    {
        auto attachment = make_unique<Attachment>();
        attachment->RenderTarget = renderTarget;
        attachment->LoadOp = loadOp;
        attachment->StoreOp = storeOp;

		_inputAttachments.emplace_back(move(attachment));
	}

	void RenderPass::CreateDepthAttachment(RenderTarget* renderTarget,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
	{
        _depth = make_unique<Attachment>();
        _depth->RenderTarget = renderTarget;
        _depth->LoadOp = loadOp;
        _depth->StoreOp = storeOp;
    }

    void RenderPass::CreateColorAttachment(RenderTarget* renderTarget,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
    {
        _color = make_unique<Attachment>();
        _color->RenderTarget = renderTarget;
        _color->LoadOp = loadOp;
        _color->StoreOp = storeOp;
    }

    void RenderPass::CreateColorResolveAttachment()
    {
        _colorResolve = make_unique<Attachment>();
        _colorResolve->LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        _colorResolve->StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    void RenderPass::CreateRenderPass()
	{
        vector<VkAttachmentDescription> attachments;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        if (_color != nullptr)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = _color->RenderTarget->Format;
            colorAttachment.samples = _color->RenderTarget->SampleCount;
            colorAttachment.loadOp = _color->LoadOp;
            colorAttachment.storeOp = _color->StoreOp;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			if (_color->LoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
				_color->LoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
			{
				colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
			else
			{
				colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}

            colorAttachment.finalLayout = _color->RenderTarget->Layout;

            attachments.push_back(colorAttachment);

			VkAttachmentReference colorAttachmentRef{};
			colorAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
			colorAttachmentRef.layout = _color->RenderTarget->Layout;

			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;
		}

		if (_colorResolve != nullptr)
		{
			VkAttachmentDescription colorAttachmentResolve{};
			colorAttachmentResolve.format = _color->RenderTarget->Format;
			colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachmentResolve.loadOp = _colorResolve->LoadOp;
			colorAttachmentResolve.storeOp = _colorResolve->StoreOp;
			colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			attachments.push_back(colorAttachmentResolve);

			VkAttachmentReference colorAttachmentResolveRef{};
			colorAttachmentResolveRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
			colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			subpass.pResolveAttachments = &colorAttachmentResolveRef;
		}

        if (_depth != nullptr)
        {
            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = _depth->RenderTarget->Format;
            depthAttachment.samples = _depth->RenderTarget->SampleCount;
            depthAttachment.loadOp = _depth->LoadOp;
            depthAttachment.storeOp = _depth->StoreOp;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = _depth->RenderTarget->Layout;

            attachments.push_back(depthAttachment);

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
            depthAttachmentRef.layout = _depth->RenderTarget->Layout;

            subpass.pDepthStencilAttachment = &depthAttachmentRef;
        }

        vector<VkAttachmentDescription> inputDescs(_inputAttachments.size());
        vector<VkAttachmentReference> inputRefs(_inputAttachments.size());
		for (size_t i = 0; i < _inputAttachments.size(); ++i)
		{
			VkAttachmentDescription inputAttachment{};
			inputAttachment.format = _inputAttachments[i]->RenderTarget->Format;
			inputAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			inputAttachment.loadOp = _inputAttachments[i]->LoadOp;
			inputAttachment.storeOp = _inputAttachments[i]->StoreOp;
			inputAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			inputAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			inputAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			inputAttachment.finalLayout = _inputAttachments[i]->RenderTarget->Layout;

            attachments.push_back(inputAttachment);

            inputDescs[i] = inputAttachment;
            inputRefs[i].attachment = static_cast<uint32_t>(attachments.size() - 1);
            inputRefs[i].layout = _inputAttachments[i]->RenderTarget->Layout;
		}

        subpass.inputAttachmentCount = static_cast<uint32_t>(inputDescs.size());
        subpass.pInputAttachments = subpass.inputAttachmentCount <= 0 ? 
            nullptr : inputRefs.data();

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(_device.GetDevice(), &renderPassInfo, nullptr, &_renderPass) != VK_SUCCESS)
			throw std::runtime_error("failed to create render pass!");
	}
}
