#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Device;
    class SwapChain;
    class Texture;
    class SyncContext;

    class GUIRenderPass : public FrameGraphPass
    {
    public:
        static constexpr const char* RT_MAIN_COLOR = "MainColor";

        // The sync context is only tapped at init: ImGui's backend wants the
        // raw graphics queue handle, which lives on the submission authority.
        GUIRenderPass(Device& device, SwapChain& swapChain, VkSampleCountFlagBits msaaSamples,
            SyncContext& syncContext);
        ~GUIRenderPass();

        const char* GetName() const override { return "GUIRenderPass"; }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

    private:
        Device& _device;
        SwapChain& _swapChain;
        VkExtent2D _extent;
        VkFormat _swapChainFormat;
        VkSampleCountFlagBits _msaaSamples;

        VkDescriptorPool _pool = VK_NULL_HANDLE;

        FGTexture _mainColor;
    };
}
