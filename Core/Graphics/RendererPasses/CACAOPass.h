#pragma once
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

#define FFX_CACAO_ENABLE_VULKAN 1
#include "ffx_cacao.h"

struct FFX_CACAO_VkContext;

namespace Core
{
    class Device;
    class Scene;
    class Texture;
    class FrameResources;
    class CommandBuffer;

    class CACAOPass
    {
    public:
        CACAOPass(Device& device, Scene& scene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples);
        ~CACAOPass();

        void EnsureRenderTargets(FrameResources& frameResources);

        bool Prepare(FrameResources& frameResources, Handle<Texture> depth, Handle<Texture> normal);
        void Record(CommandBuffer& commandBuffer);
        void UpdateGUI();

    private:
        FFX_CACAO_VkContext* GetOrCreateCacaoContext(
            FrameResources* frameKey,
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
        Device& _device;
        Scene& _scene;
        VkExtent2D _screenExtent;
        VkSampleCountFlagBits _msaaSamples;

        Handle<Texture> _aoTexture;

        unordered_map<FrameResources*, FFX_CACAO_VkContext*> m_cacaoContexts;

        Settings m_settings;

        // Stashed by Prepare, consumed by Record.
        FFX_CACAO_VkContext* _currentContext = nullptr;
        FFX_CACAO_Matrix4x4 _proj{};
        FFX_CACAO_Matrix4x4 _normalsToView{};

        VkDescriptorSet _aoImGuiDS = VK_NULL_HANDLE;
    };
}
