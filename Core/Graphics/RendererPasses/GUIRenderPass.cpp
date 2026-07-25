#include "stdafx.h"
#include "GUIRenderPass.h"
#include "Graphics/Vulkans/SwapChain.h"

using namespace Core;

Core::GUIRenderPass::GUIRenderPass(Device& device, WorkerThreadManager& workerThreadManager,
    SwapChain& swapChain, VkSampleCountFlagBits msaaSamples)
    : RendererPass(device, workerThreadManager)
    , _swapChain(swapChain)
    , _msaaSamples(msaaSamples)
    , _swapChainFormat(swapChain.GetImageFormat())
{
    _extent = swapChain.GetSwapChainExtent();

    _renderPass->CreateColorAttachment(_swapChainFormat, _msaaSamples,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    _renderPass->CreateColorResolveAttachment();

    _renderPass->CreateRenderPass();

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

    //this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _device.GetInstance();
    init_info.PhysicalDevice = _device.GetPhysicalDevice();
    init_info.Device = _device.GetDevice();
    init_info.Queue = _device.GetGraphicsQueue();
    init_info.DescriptorPool = _pool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_8_BIT;
    init_info.RenderPass = _renderPass->GetHandle();
    ImGui_ImplVulkan_Init(&init_info);
}

Core::GUIRenderPass::~GUIRenderPass()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(_device.GetDevice(), _pool, nullptr);
}

void Core::GUIRenderPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc colorDesc{};
    colorDesc.extent = _extent;
    colorDesc.format = _swapChainFormat;
    colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    colorDesc.samples = _msaaSamples;
    colorDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    renderFrame.GetResources().GetOrCreateRenderTarget(RT_MAIN_COLOR, colorDesc);
}

void Core::GUIRenderPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
    auto& frameResources = renderFrame.GetResources();
    auto colorTarget = frameResources.GetRenderTarget(RT_MAIN_COLOR);

	// Create framebuffer using swapchain image view as resolve attachment for each frame
	// This framebuffer is referencing different swapchain image for each frame, so we create it per frame
    string framebufferName = "GUIRenderPass_" + to_string(imageIndex);

    auto* framebuffer = frameResources.GetFramebuffer(framebufferName);
    if (!framebuffer)
    {
        vector<VkImageView> imageViews = {
            colorTarget.Get().GetImageView(),
            _swapChain.GetImageView(imageIndex)  // Resolve target
        };

        auto fb = make_unique<Framebuffer>(_device, *_renderPass, imageViews, _extent);
        framebuffer = fb.get();
        frameResources.RegisterFramebuffer(framebufferName, std::move(fb));
    }

    commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

    auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*framebuffer);
    commandBuffer.BeginRenderPass(renderPassBeginInfo);

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.GetHandle());

    commandBuffer.EndRenderPass();
}