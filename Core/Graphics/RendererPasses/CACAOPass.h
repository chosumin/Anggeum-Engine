#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "ResolvePass.h"

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

        void EnsureRenderTargets(RenderFrame& renderFrame) override;
        void Draw(RenderFrame& renderFrame, SyncContext& syncContext, CommandBuffer& commandBuffer, uint32_t imageIndex) override;

        void UpdateGUI();

    private:
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
            int      QualityLevel                     = 3;    // FFX_CACAO_QUALITY_HIGH
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

        unordered_map<RenderFrame*, FFX_CACAO_VkContext*> m_cacaoContexts;

        Settings m_settings;

        VkDescriptorSet _aoImGuiDS = VK_NULL_HANDLE;
    };
}
