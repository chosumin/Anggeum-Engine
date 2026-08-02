#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Device;
    class SwapChain;
    class Texture;

    class FGGUIRenderPass : public FrameGraphPass
    {
    public:
        static constexpr const char* RT_MAIN_COLOR = "MainColor";

        FGGUIRenderPass(Device& device, SwapChain& swapChain, VkSampleCountFlagBits msaaSamples);
        ~FGGUIRenderPass();

        const char* GetName() const override { return "FGGUIRenderPass"; }

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
