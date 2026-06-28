#pragma once
#include "Graphics/RendererPass.h"

namespace Core
{
    class SwapChain;

    class GUIRenderPass : public RendererPass
    {
    public:
        static constexpr const char* RT_MAIN_COLOR = "MainColor";

        GUIRenderPass(Device& device, WorkerThreadManager& workerThreadManager, 
            SwapChain& swapChain, VkSampleCountFlagBits msaaSamples);
        ~GUIRenderPass();

        void Prepare() override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;
    private:
        void EnsureRenderTargets(RenderFrame& renderFrame);
    private:
        SwapChain& _swapChain;
        VkExtent2D _extent;
        VkFormat _swapChainFormat;
        VkSampleCountFlagBits _msaaSamples;

        VkDescriptorPool _pool;
    };
}

