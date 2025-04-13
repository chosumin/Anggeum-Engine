#include "stdafx.h"
#include "RenderPass.h"
#include "Utils/Utility.h"
#include "SwapChain.h"
#include "Framebuffer.h"

namespace Core
{
    RenderPass::RenderPass(Device& device)  
       : _device{device}, _renderPass{VK_NULL_HANDLE}
    {  
    }

	RenderPass::~RenderPass()
	{
		if (_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(_device.GetDevice(), _renderPass, nullptr);
		}
	}

    VkRenderPassBeginInfo RenderPass::CreateRenderPassBeginInfo(
        Framebuffer& framebuffer, uint32_t imageIndex)
    {
        auto framebufferHandle = framebuffer.GetHandle(imageIndex);
        VkExtent2D swapChainExtent = framebuffer.GetExtent();

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = _renderPass;
        renderPassInfo.framebuffer = framebufferHandle;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChainExtent;

        VkClearValue clearValue{};

        if (_color != nullptr)
        {
            clearValue.color = { {0.25f, 0.25f, 0.25f, 1.0f} };
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
            attachments.push_back(_color->RenderTarget->GetImageView());
        }

        if (_colorResolve != nullptr)
        {
            attachments.push_back(swapChainImageView);
        }

        if (_depth != nullptr)
        {
            attachments.push_back(_depth->RenderTarget->GetImageView());
        }

        for (auto& renderTarget : _inputAttachments)
        {
            attachments.push_back(renderTarget->RenderTarget->GetImageView());
        }

        return attachments;
    }

    void RenderPass::CreateAttachment(Texture* renderTarget,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
    {
        auto attachment = make_unique<Attachment>();
        attachment->RenderTarget = renderTarget;
        attachment->LoadOp = loadOp;
        attachment->StoreOp = storeOp;

		_inputAttachments.emplace_back(move(attachment));
	}

	void RenderPass::CreateDepthAttachment(Texture* renderTarget,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
	{
        _depth = make_unique<Attachment>();
        _depth->RenderTarget = renderTarget;
        _depth->LoadOp = loadOp;
        _depth->StoreOp = storeOp;
    }

    void RenderPass::CreateColorAttachment(Texture* renderTarget,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp, VkImageLayout finalLayout)
    {
        _color = make_unique<Attachment>();
        _color->RenderTarget = renderTarget;
        _color->LoadOp = loadOp;
        _color->StoreOp = storeOp;
		_color->FinalLayout = finalLayout;
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
            colorAttachment.format = _color->RenderTarget->GetFormat();
            colorAttachment.samples = _color->RenderTarget->GetSampleCount();
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

            colorAttachment.finalLayout = _color->FinalLayout;

            attachments.push_back(colorAttachment);

			VkAttachmentReference colorAttachmentRef{};
			colorAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;
		}

		if (_colorResolve != nullptr)
		{
			VkAttachmentDescription colorAttachmentResolve{};
			colorAttachmentResolve.format = _color->RenderTarget->GetFormat();
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
            depthAttachment.format = _depth->RenderTarget->GetFormat();
            depthAttachment.samples = _depth->RenderTarget->GetSampleCount();
            depthAttachment.loadOp = _depth->LoadOp;
            depthAttachment.storeOp = _depth->StoreOp;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            attachments.push_back(depthAttachment);

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            subpass.pDepthStencilAttachment = &depthAttachmentRef;
        }

        vector<VkAttachmentDescription> inputDescs(_inputAttachments.size());
        vector<VkAttachmentReference> inputRefs(_inputAttachments.size());
		for (size_t i = 0; i < _inputAttachments.size(); ++i)
		{
			VkAttachmentDescription inputAttachment{};
			inputAttachment.format = _inputAttachments[i]->RenderTarget->GetFormat();
			inputAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			inputAttachment.loadOp = _inputAttachments[i]->LoadOp;
			inputAttachment.storeOp = _inputAttachments[i]->StoreOp;
			inputAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			inputAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			inputAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			inputAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            attachments.push_back(inputAttachment);

            inputDescs[i] = inputAttachment;
            inputRefs[i].attachment = static_cast<uint32_t>(attachments.size() - 1);
            inputRefs[i].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

        subpass.inputAttachmentCount = static_cast<uint32_t>(inputDescs.size());
        subpass.pInputAttachments = subpass.inputAttachmentCount <= 0 ? 
            nullptr : inputRefs.data();

        array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask =
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies[1].dstStageMask =
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].dstAccessMask =
            VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

		if (vkCreateRenderPass(_device.GetDevice(), &renderPassInfo, nullptr, &_renderPass) != VK_SUCCESS)
			throw std::runtime_error("failed to create render pass!");
	}
}
