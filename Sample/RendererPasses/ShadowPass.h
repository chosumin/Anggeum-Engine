#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
    class Scene;
    class SwapChain;
    class Material;

    class ShadowPass : public RendererPass
    {
    public:
        static constexpr const char* RT_SHADOW_DEPTH = "ShadowDepth";

        ShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, SwapChain& swapChain, VkFormat depthFormat, TransformBatch& transformBatch);
        ~ShadowPass();

        void Prepare() override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

        ShadowUniform* GetShadowBuffer() { return &_shadowBuffer; }

    private:
        void EnsureRenderTargets(RenderFrame& renderFrame);

    private:
        Scene& _scene;
        VkExtent2D _shadowExtent;

        unique_ptr<RendererBatches> _rendererBatches;
        
        ShadowUniform _shadowBuffer;
        CameraBuffer _directionalLight;

        shared_ptr<Material> _shadowMaterial;

        VkSampleCountFlagBits _msaaSamples;

        bool _initialized = false;
    };
}

