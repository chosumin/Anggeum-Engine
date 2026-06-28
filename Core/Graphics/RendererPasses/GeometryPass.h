#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/RendererBatch.h"

namespace Core
{
    class Scene;
    class SwapChain;
    class Buffer;
    class RenderContext;

    class GeometryPass : public RendererPass
    {
    public:
        static constexpr const char* RT_MAIN_COLOR = "MainColor";
        static constexpr const char* RT_MAIN_DEPTH = "MainDepth";
        static constexpr const char* RT_SHADOW_DEPTH = "ShadowDepth";
        static constexpr const char* RT_IRRADIANCE = "Irradiance";
        static constexpr const char* RT_PREFILTERED = "Prefiltered";
        static constexpr const char* RT_BRDF_LUT = "BrdfLut";
        static constexpr const char* RT_OFFSCREEN = "Offscreen";

        GeometryPass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, SwapChain& swapChain, VkFormat depthFormat,
            VkSampleCountFlagBits msaaSamples, ShadowUniform& shadowBuffer,
            Buffer* lightVisibilityBuffer, ivec2 tileNums,
            TransformBatch& transformBatch);
        ~GeometryPass();

        void EnsureRenderTargets(RenderFrame& renderFrame) override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;
    private:
        void RegisterGiTexturesToBindless(RenderFrame& renderContext);

        void EnsureIBLResources(RenderFrame& renderFrame);

        void PreparePregenerationSkybox(RenderFrame& renderFrame);
        void DrawSkybox(RenderFrame& renderFrame, CommandBuffer& commandBuffer);
        void UpdateLightBuffer();

    private:
        Scene& _scene;
        VkSampleCountFlagBits _msaaSamples;
        VkFormat _swapChainFormat;

        unique_ptr<RendererBatches> _rendererBatches;
        Pipeline* _skyboxPipeline = nullptr;

        GI _giBuffer;
        ShadowUniform& _shadowBuffer;
        LightBuffer _lightBuffer;
        Buffer* _lightVisibilityBuffer;
        TileInfo _tileInfo;

        bool _iblGenerated = false;

        shared_ptr<Texture> _offscreenTexture;
        shared_ptr<Texture> _irradianceCubemap;
        shared_ptr<Texture> _prefilteredCubemap;
        shared_ptr<Texture> _brdfLut;

        // Pass 2 RenderPass (color LOAD, depth LOAD)
        RenderPass* _renderPassPass2 = nullptr;
    };
}

