#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "ResolvePass.h"

namespace Core
{
    class Scene;
    class SDFGenerator;

    class DFAOPass : public RendererPass
    {
    public:
        DFAOPass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples,
            SDFGenerator* sdfGenerator);
        ~DFAOPass();

        void EnsureRenderTargets(RenderFrame& renderFrame) override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

        void UpdateGUI();

    private:
        void UpdateParams();

    private:
        Scene& _scene;
        VkExtent2D _screenExtent;
        VkSampleCountFlagBits _msaaSamples;
        SDFGenerator* _sdfGenerator;

        shared_ptr<Texture> _aoTexture;

        shared_ptr<Shader>   _dfaoShader;
        unique_ptr<Pipeline> _dfaoPipeline;

        DFAOUniform _params{};

        int   _numSamples  = 8;
        float _maxDistance = 2.0f;
        float _intensity   = 1.0f;
        float _stepScale   = 0.5f;
        float _contactShadowStrength = 0.7f;
        float _contactThreshold = 0.05f;

        VkDescriptorSet _aoImGuiDS = VK_NULL_HANDLE;
    };
}