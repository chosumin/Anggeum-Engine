#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
    class Scene;
    class SDFGenerator;

    class DFAOPass : public RendererPass
    {
    public:
        static constexpr const char* RT_DFAO            = "DFAOResult";
        static constexpr const char* RT_NORMAL_RESOLVED = "DFAONormalResolved";

        DFAOPass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples,
            SDFGenerator* sdfGenerator);
        ~DFAOPass();

        void Prepare() override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

        static bool IsEnabled() { return _enabled; }

    private:
        void EnsureRenderTargets(RenderFrame& renderFrame);
        void ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            shared_ptr<Texture> msaaNormal);
        void UpdateParams();
        void UpdateGUI();

    private:
        Scene& _scene;
        VkExtent2D _screenExtent;
        VkSampleCountFlagBits _msaaSamples;
        SDFGenerator* _sdfGenerator;

        shared_ptr<Texture> _aoTexture;
        shared_ptr<Texture> _resolvedNormalTexture;

        shared_ptr<Shader>   _dfaoShader;
        unique_ptr<Pipeline> _dfaoPipeline;

        shared_ptr<Shader>   _normalResolveShader;
        unique_ptr<Pipeline> _normalResolvePipeline;

        DFAOUniform _params{};

        inline static bool _enabled    = false;
        bool  _showWindow  = false;
        int   _numSamples  = 8;
        float _maxDistance = 2.0f;
        float _intensity   = 1.0f;
        float _stepScale   = 0.5f;
        float _contactShadowStrength = 0.7f;  // Contact shadow enhancement strength
        float _contactThreshold = 0.05f;      // Contact detection distance threshold

        VkDescriptorSet _aoImGuiDS = VK_NULL_HANDLE;
    };
}