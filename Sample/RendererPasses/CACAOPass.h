#pragma once
#include "Core/Graphics/RendererPass.h"
#include "Core/Graphics/BufferObjects.h"
#include <ffx_cacao_impl.h>

namespace Core
{
    class Scene;
    class CACAOPass : public RendererPass
    {
    public:
        CACAOPass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples);
        ~CACAOPass();

        void Prepare() override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

        static bool IsEnabled() { return _enabled; }

    private:
        void EnsureRenderTargets(RenderFrame& renderFrame);
        void ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            shared_ptr<Texture> msaaNormal);
        void UpdateGUI();

        struct Settings
        {
            float    Radius                           = 1.2f;
            float    ShadowMultiplier                 = 1.0f;
            float    ShadowPower                      = 1.5f;
            float    ShadowClamp                      = 0.98f;
            float    HorizonAngleThreshold            = 0.06f;
            float    FadeOutFrom                      = 50.0f;
            float    FadeOutTo                        = 300.0f;
            int      QualityLevel                     = 2;    // FFX_CACAO_QUALITY_HIGH
            float    AdaptiveQualityLimit             = 0.45f;
            int BlurPassCount                    = 2;
            float    Sharpness                        = 0.98f;
            float    DetailShadowStrength             = 0.5f;
            bool     GenerateNormals                  = false;
            float    BilateralSigmaSquared            = 5.0f;
            float    BilateralSimilarityDistanceSigma = 0.01f;
        };

    private:
        Scene& _scene;
        VkExtent2D _screenExtent;
        VkSampleCountFlagBits _msaaSamples;

        shared_ptr<Texture> _aoTexture;
        shared_ptr<Texture> _resolvedNormalTexture;

        shared_ptr<Shader>   _normalResolveShader;
        unique_ptr<Pipeline> _normalResolvePipeline;

        // Opaque context - must be heap-allocated via FFX_CACAO_VkGetContextSize()
        FFX_CACAO_VkContext* m_cacaoContext = nullptr;

        Settings m_settings;

        inline static bool _enabled = true;
        bool _showWindow = false;

        VkDescriptorSet _aoImGuiDS = VK_NULL_HANDLE;
    };
}