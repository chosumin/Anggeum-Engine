#include "stdafx.h"
#include "FGGUIRenderPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"

using namespace Core;

FGGUIRenderPass::FGGUIRenderPass(Device& device, SwapChain& swapChain,
    VkSampleCountFlagBits msaaSamples)
    : _device(device)
    , _swapChain(swapChain)
    , _msaaSamples(msaaSamples)
    , _swapChainFormat(swapChain.GetImageFormat())
{
    _extent = swapChain.GetSwapChainExtent();

    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = static_cast<uint32_t>(size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    vkCreateDescriptorPool(_device.GetDevice(), &pool_info, nullptr, &_pool);

    //this initializes the core structures of imgui
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui_ImplGlfw_InitForVulkan(Core::Window::Instance().GetWindow(), true);

    //this initializes imgui for Vulkan, rendering via dynamic rendering
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _device.GetInstance();
    init_info.PhysicalDevice = _device.GetPhysicalDevice();
    init_info.Device = _device.GetDevice();
    init_info.Queue = _device.GetGraphicsQueue();
    init_info.DescriptorPool = _pool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = _msaaSamples;
    init_info.UseDynamicRendering = true;
    init_info.PipelineRenderingCreateInfo = {};
    init_info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    // Points at the member so it stays valid: the backend copies the InitInfo
    // struct but not the array behind this pointer.
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapChainFormat;
    ImGui_ImplVulkan_Init(&init_info);
}

FGGUIRenderPass::~FGGUIRenderPass()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(_device.GetDevice(), _pool, nullptr);
}

void FGGUIRenderPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderFrame& renderFrame)
{
	_mainColor = builder.GetTexture(RT_MAIN_COLOR);
	builder.Read(_mainColor, TextureAccess::SampledFragment);

	// The resolve target is the raw swapchain image (no graph declaration) and
    // ImGui's draw data is external state.
    builder.SetManualRendering();
    builder.SetSideEffect();

    // Finalize this frame's ImGui draw lists on the main thread; the worker
    // only replays them. Valid until the next ImGui::NewFrame.
    ImGui::Render();
}

void FGGUIRenderPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    const uint32_t imageIndex = context.GetImageIndex();
    VkImage swapChainImage = _swapChain.GetImage(imageIndex);

    // Acquire-to-attachment: contents are discarded (the resolve overwrites
    // every pixel). The imageAvailable semaphore wait (first graphics submit)
    // is scoped to COLOR_ATTACHMENT_OUTPUT, matching this barrier's dst stage.
    commandBuffer.CreateBarrierBatch()
        .Image(swapChainImage, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)
        .Submit();

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = context.GetTexture(_mainColor).GetImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colorAttachment.resolveImageView = _swapChain.GetImageView(imageIndex);
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, _extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    commandBuffer.SetViewportAndScissor(_extent);
    commandBuffer.BeginRendering(renderingInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.GetHandle());

    commandBuffer.EndRendering();

    // Hand the resolved image to the presentation engine.
    commandBuffer.CreateBarrierBatch()
        .Image(swapChainImage, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE)
        .Submit();
}
