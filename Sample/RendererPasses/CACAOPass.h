#pragma once
#include "Core/Graphics/RendererPass.h"
#include "Core/Graphics/BufferObjects.h"

#define FFX_CACAO_ENABLE_VULKAN 1
#include "ffx_cacao.h"

struct FFX_CACAO_VkContext;

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

        // Create and initialize a CACAO context for the given RenderFrame.
        // Each RenderFrame corresponds to one frame-in-flight slot, so this
        // ensures the context is bound to the correct per-frame render targets.
        FFX_CACAO_VkContext* GetOrCreateCacaoContext(
            RenderFrame* frameKey,
            VkImageView depthView,
            VkImageView normalsView,
            VkImage outputImage,
            VkImageView outputView);

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
            int      BlurPassCount                    = 2;
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

        // One CACAO context per RenderFrame (i.e., per frame-in-flight slot).
        // Key: RenderFrame pointer (unique per slot, stable lifetime managed by RenderContext).
        unordered_map<RenderFrame*, FFX_CACAO_VkContext*> m_cacaoContexts;

        Settings m_settings;

        inline static bool _enabled = true;
        bool _showWindow = false;

        VkDescriptorSet _aoImGuiDS = VK_NULL_HANDLE;
    };
}