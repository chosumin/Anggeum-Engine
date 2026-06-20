#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/RendererBatch.h"

namespace Core
{
    class Scene;
    class SwapChain;
    class Material;

    class DepthPrePass : public RendererPass
    {
    public:
        static constexpr const char* RT_MAIN_DEPTH  = "MainDepth";
        static constexpr const char* RT_MAIN_NORMAL = "MainNormal";

        DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, SwapChain& swapChain, VkFormat depthFormat,
            VkSampleCountFlagBits msaaSamples, TransformBatch& transformBatch);
        ~DepthPrePass();

        void Prepare() override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

        RendererBatches* GetRendererBatches() const { return _rendererBatches.get(); }

    private:
        void EnsureRenderTargets(RenderFrame& renderFrame);

    private:
        Scene& _scene;
        VkExtent2D _extent;
        VkSampleCountFlagBits _msaaSamples;

        unique_ptr<RendererBatches> _rendererBatches;

        shared_ptr<Material> _depthMaterial;
    };
}

