#pragma once
#include "Graphics/RendererPass.h"

namespace Core
{
    class Scene;
    class SwapChain;
    class RendererBatch;

    class DepthPrePass : public RendererPass
    {
    public:
        static constexpr const char* RT_MAIN_DEPTH  = "MainDepth";
        static constexpr const char* RT_MAIN_NORMAL = "MainNormal";

        DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, SwapChain& swapChain, VkFormat depthFormat,
            VkSampleCountFlagBits msaaSamples);
        ~DepthPrePass();

        void EnsureRenderTargets(RenderFrame& renderFrame) override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

    private:
        Scene& _scene;
        VkExtent2D _extent;
        VkSampleCountFlagBits _msaaSamples;

        shared_ptr<Shader> _depthNormalShader;
        Pipeline* _pipeline = nullptr;
    };
}

