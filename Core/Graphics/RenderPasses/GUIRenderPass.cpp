#include "stdafx.h"
#include "GUIRenderPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/SyncContext.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/FrameCounter.h"
#include "Utils/Log.h"
#include "stb_image_write.h"

using namespace Core;

GUIRenderPass::GUIRenderPass(Device& device, SwapChain& swapChain,
    VkSampleCountFlagBits msaaSamples, SyncContext& syncContext)
    : _device(device)
    , _swapChain(swapChain)
    , _msaaSamples(msaaSamples)
    , _swapChainFormat(swapChain.GetImageFormat())
{
    _extent = swapChain.GetSwapChainExtent();

    if (const char* env = getenv("ANGGEUM_SCREENSHOT"))
    {
        string spec = env;
        size_t at = spec.find("@");
        _screenshotEnvPath = spec.substr(0, at);
        _screenshotEnvFrame = at == string::npos
            ? 1 : strtoull(spec.c_str() + at + 1, nullptr, 10);
    }

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
    init_info.Queue = syncContext.GetGraphicsQueueForExternalInit();
    init_info.DescriptorPool = _pool;
	const uint32_t imageCount = static_cast<uint32_t>(_swapChain.GetSwapChainCount());
	init_info.MinImageCount = imageCount;
	init_info.ImageCount = imageCount;
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

GUIRenderPass::~GUIRenderPass()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(_device.GetDevice(), _pool, nullptr);
}

void GUIRenderPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderFrame& renderFrame)
{
	_mainColor = builder.GetTexture(RT_MAIN_COLOR);
	builder.Write(_mainColor, TextureAccess::ColorLoadWrite);

    PollScreenshotRequest();
    uint32_t slot = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);
    if (_screenshots[slot].pending)
        WriteScreenshot(slot);

    _captureThisFrame = !_screenshotRequest.empty();
    if (_captureThisFrame)
    {
        ScreenshotCapture& capture = _screenshots[slot];
        capture.buffer = frameResources.GetOrCreateStorageBuffer("Screenshot.Readback",
            { VkDeviceSize(_extent.width) * _extent.height * 4,
              VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::UNIFORM });
        capture.path = _screenshotRequest;
        capture.pending = true;
        _screenshotRequest.clear();

        _captureBuffer = builder.ImportBuffer("Screenshot.Readback", capture.buffer);
        builder.Write(_captureBuffer, BufferAccess::TransferDst);
    }

	// The resolve target is the raw swapchain image (no graph declaration) and
    // ImGui's draw data is external state.
    builder.SetSideEffect();

    // Finalize this frame's ImGui draw lists on the main thread; the worker
    // only replays them. Valid until the next ImGui::NewFrame.
    ImGui::Render();
}

void GUIRenderPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
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

    // Built by hand: the resolve target is a raw swapchain view, which the
    // typed helper (whole-texture views) cannot express.
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = context.GetTexture(_mainColor).GetImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colorAttachment.resolveImageView = _swapChain.GetImageView(imageIndex);
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    RenderingSetup setup;
    setup.renderArea = _extent;
    setup.colorAttachments.push_back(colorAttachment);

    commandBuffer.SetViewportAndScissor(_extent);
    commandBuffer.BeginRendering(setup);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.GetHandle());

    commandBuffer.EndRendering();

    if (_captureThisFrame)
    {
        // Read the resolved frame back on its way to the presentation engine.
        commandBuffer.CreateBarrierBatch()
            .Image(swapChainImage, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT)
            .Submit();

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { _extent.width, _extent.height, 1 };
        vkCmdCopyImageToBuffer(commandBuffer.GetHandle(), swapChainImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            context.GetBuffer(_captureBuffer).GetBuffer(), 1, &region);

        commandBuffer.CreateBarrierBatch()
            .Image(swapChainImage, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE)
            .Submit();
        return;
    }

    // Hand the resolved image to the presentation engine.
    commandBuffer.CreateBarrierBatch()
        .Image(swapChainImage, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE)
        .Submit();
}

void GUIRenderPass::PollScreenshotRequest()
{
    // One request per press; a focused ImGui text field keeps the key.
    bool held = Input::KeyPressed[KeyCode::F11] && !ImGui::GetIO().WantTextInput;
    if (held && !_screenshotKeyHeld)
        _screenshotRequest = "screenshot_" + to_string(FrameCounter::GetFrameNumber()) + ".png";
    _screenshotKeyHeld = held;

    if (!_screenshotEnvPath.empty() && FrameCounter::GetFrameNumber() >= _screenshotEnvFrame)
    {
        _screenshotRequest = _screenshotEnvPath;
        _screenshotEnvPath.clear();
    }
}

void GUIRenderPass::WriteScreenshot(uint32_t slot)
{
    ScreenshotCapture& capture = _screenshots[slot];
    capture.pending = false;

    void* mapped = nullptr;
    capture.buffer.Get().GetMappedPtr(&mapped);
    const uint8_t* pixels = static_cast<const uint8_t*>(mapped);

    // Swapchains are BGRA nearly everywhere; PNG wants RGBA, opaque.
    const bool bgra = _swapChainFormat == VK_FORMAT_B8G8R8A8_UNORM
        || _swapChainFormat == VK_FORMAT_B8G8R8A8_SRGB;
    vector<uint8_t> rgba(size_t(_extent.width) * _extent.height * 4);
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i + 0] = pixels[i + (bgra ? 2 : 0)];
        rgba[i + 1] = pixels[i + 1];
        rgba[i + 2] = pixels[i + (bgra ? 0 : 2)];
        rgba[i + 3] = 255;
    }

    int ok = stbi_write_png(capture.path.c_str(), int(_extent.width), int(_extent.height), 4,
        rgba.data(), int(_extent.width) * 4);
    LOG("Screenshot %s: %s", ok ? "written" : "FAILED", capture.path.c_str());
}
