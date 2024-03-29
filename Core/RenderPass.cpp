#include "stdafx.h"
#include "RenderPass.h"
#include "Utility.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "RenderContext.h"
#include "Framebuffer.h"

namespace Core
{
	RenderPass::RenderPass(Device& device)
        :_device{device}
	{
        _msaaSamples = GetMaxUsableSampleCount();

        auto a = std::bind(&RenderPass::Resize, this, std::placeholders::_1);
        Core::RenderContext::AddResizeCallback(a);
	}

	RenderPass::~RenderPass()
	{
        Cleanup();

        auto a = std::bind(&RenderPass::Resize, this, std::placeholders::_1);
        Core::RenderContext::RemoveResizeCallback(a);

        delete(_framebuffer);
	}

    VkRenderPassBeginInfo RenderPass::CreateRenderPassBeginInfo(VkFramebuffer framebuffer, VkExtent2D swapChainExtent)
    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = _renderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChainExtent;

        if (_color != nullptr)
        {
            VkClearValue clearValue{};
            clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
            _clearValues.push_back(clearValue);
            _clearValues.push_back(clearValue);
        }

        if (_depth != nullptr)
        {
            VkClearValue clearValue{};
            clearValue.depthStencil = { 1.0f, 0 };
            _clearValues.emplace_back(clearValue);
        }

        for (auto& renderTarget : _inputRenderTargets)
        {
            VkClearValue clearValue{};
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
            attachments.push_back(_color->ImageView);
            attachments.push_back(swapChainImageView);
        }

        if (_depth != nullptr)
        {
            attachments.push_back(_depth->ImageView);
        }

        for (auto& renderTarget : _inputRenderTargets)
        {
            attachments.push_back(renderTarget->ImageView);
        }

        return attachments;
    }

    void RenderPass::Cleanup()
	{
        auto device = _device.GetDevice();
		for (size_t i = 0; i < _inputRenderTargets.size(); ++i)
		{
			vkDestroyImageView(device, _inputRenderTargets[i]->ImageView, nullptr);
			vkDestroyImage(device, _inputRenderTargets[i]->Image, nullptr);
			vkFreeMemory(device, _inputRenderTargets[i]->ImageMemory, nullptr);
		}

        if (_color != nullptr)
        {
            vkDestroyImageView(device, _color->ImageView, nullptr);
            vkDestroyImage(device, _color->Image, nullptr);
            vkFreeMemory(device, _color->ImageMemory, nullptr);
        }

        if (_depth != nullptr)
        {
            vkDestroyImageView(device, _depth->ImageView, nullptr);
            vkDestroyImage(device, _depth->Image, nullptr);
            vkFreeMemory(device, _depth->ImageMemory, nullptr);
        }

        vkDestroyRenderPass(device, _renderPass, nullptr);
    }

    void RenderPass::Resize(SwapChain& swapChain)
    {
        Cleanup();

        VkExtent2D extent = swapChain.GetSwapChainExtent();

        if (_color != nullptr)
        {
            CreateColorRenderTarget(extent, _color->Format);
        }

        if (_depth != nullptr)
        {
            CreateDepthRenderTarget(extent);
        }

        for (auto& renderTarget : _inputRenderTargets)
        {
            CreateRenderTarget(extent, renderTarget->Format, renderTarget->Layout, renderTarget->UsageFlags);
        }
    }

    void RenderPass::CreateRenderTarget(VkExtent2D extent, VkFormat format, VkImageLayout layout, VkImageUsageFlags usageFlags)
    {
        VkImage image;
        VkImageView imageView;
        VkDeviceMemory memory;

        Utility::CreateImage(_device, extent.width, extent.height, 1,
            _msaaSamples, format, VK_IMAGE_TILING_OPTIMAL,
            usageFlags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        imageView = Utility::CreateImageView(_device, image, format, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        auto renderTarget = make_unique<RenderTarget>();
        renderTarget->Format = format;
        renderTarget->Layout = layout;
        renderTarget->Image = image;
        renderTarget->ImageMemory = memory;
        renderTarget->ImageView = imageView;
        renderTarget->UsageFlags = usageFlags;

		_inputRenderTargets.emplace_back(move(renderTarget));
	}

	void RenderPass::CreateDepthRenderTarget(VkExtent2D extent)
	{
        VkImage image;
        VkImageView imageView;
        VkDeviceMemory memory;

		auto depthFormat = _device.FindSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

		Utility::CreateImage(_device, extent.width, extent.height, 1,
            _msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        imageView = Utility::CreateImageView(_device, 
            image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        _depth = make_unique<RenderTarget>();
        _depth->Format = depthFormat;
        _depth->Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        _depth->Image = image;
        _depth->ImageMemory = memory;
        _depth->ImageView = imageView;
    }

    void RenderPass::CreateColorRenderTarget(VkExtent2D extent, VkFormat format)
    {
        VkImage image;
        VkImageView imageView;
        VkDeviceMemory memory;

        //VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : gpu virtual address and not physical memory pages.
        //프레임버퍼에 사용되며, 싱글 렌더 패스 동안에만 존재함.
        Utility::CreateImage(_device, extent.width, extent.height, 1,
            _msaaSamples, format, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        imageView = Utility::CreateImageView(_device, 
            image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        _color = make_unique<RenderTarget>();
        _color->Format = format;
        _color->Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        _color->Image = image;
        _color->ImageMemory = memory;
        _color->ImageView = imageView;
    }

    void RenderPass::CreateRenderPass()
	{
        vector<VkAttachmentDescription> attachments;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        if (_color != nullptr)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = _color->Format;
            colorAttachment.samples = _msaaSamples;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = _color->Layout;

            attachments.push_back(colorAttachment);

            VkAttachmentReference colorAttachmentRef{};
            colorAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
            colorAttachmentRef.layout = _color->Layout;

            VkAttachmentDescription colorAttachmentResolve{};
            colorAttachmentResolve.format = _color->Format;
            colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            attachments.push_back(colorAttachmentResolve);

            VkAttachmentReference colorAttachmentResolveRef{};
            colorAttachmentResolveRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
            colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachmentRef;
            subpass.pResolveAttachments = &colorAttachmentResolveRef;
        }

        if (_depth != nullptr)
        {
            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = _depth->Format;
            depthAttachment.samples = _msaaSamples;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = _depth->Layout;

            attachments.push_back(depthAttachment);

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
            depthAttachmentRef.layout = _depth->Layout;

            subpass.pDepthStencilAttachment = &depthAttachmentRef;
        }

        vector<VkAttachmentDescription> inputDescs(_inputRenderTargets.size());
        vector<VkAttachmentReference> inputRefs(_inputRenderTargets.size());
		for (size_t i = 0; i < _inputRenderTargets.size(); ++i)
		{
			VkAttachmentDescription inputAttachment{};
			inputAttachment.format = _inputRenderTargets[i]->Format;
			inputAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			inputAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			inputAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			inputAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			inputAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			inputAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			inputAttachment.finalLayout = _inputRenderTargets[i]->Layout;

            attachments.push_back(inputAttachment);

            inputDescs[i] = inputAttachment;
            inputRefs[i].attachment = static_cast<uint32_t>(attachments.size() - 1);
            inputRefs[i].layout = _inputRenderTargets[i]->Layout;
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

    VkSampleCountFlagBits RenderPass::GetMaxUsableSampleCount()
    {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(
            _device.GetPhysicalDevice(),
            &physicalDeviceProperties);

        VkSampleCountFlags counts =
            physicalDeviceProperties.limits.framebufferColorSampleCounts &
            physicalDeviceProperties.limits.framebufferDepthSampleCounts;

        if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
        if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
        if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
        if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
        if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
        if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

        return VK_SAMPLE_COUNT_1_BIT;
    }
}
