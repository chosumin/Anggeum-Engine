#pragma once
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Device;
    class Scene;
    class Shader;
    class Pipeline;
    class Texture;
    class Buffer;
    class SDFGenerator;
    class FrameResources;
    class CommandBuffer;
    class FrameGraphPassContext;

    class DFAOPass
    {
    public:
        DFAOPass(Device& device, Scene& scene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples,
            SDFGenerator* sdfGenerator);
        ~DFAOPass();

        void EnsureRenderTargets(FrameResources& frameResources);

        bool Prepare(FrameResources& frameResources);

        void Record(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            Texture& depth, Texture& normal);

        void UpdateGUI();

    private:
        void UpdateParams();

    private:
        struct DFAOPushConstants
        {
            glm::mat4 InvView;
            glm::mat4 InvProj;
            glm::vec2 ScreenSize;
            float     _pad[2];
        };

        Device& _device;
        Scene& _scene;
        VkExtent2D _screenExtent;
        VkSampleCountFlagBits _msaaSamples;
        SDFGenerator* _sdfGenerator;

        Handle<Texture> _aoTexture;

        Handle<Shader>       _dfaoShader;
        unique_ptr<Pipeline> _dfaoPipeline;

        DFAOUniform _params{};

        // Stashed by Prepare, consumed by Record.
        Buffer* _paramsBuffer = nullptr;
        DFAOPushConstants _pushConstants{};

        int   _numSamples  = 8;
        float _maxDistance = 2.0f;
        float _intensity   = 1.0f;
        float _stepScale   = 0.5f;
        float _contactShadowStrength = 0.7f;
        float _contactThreshold = 0.05f;

        VkDescriptorSet _aoImGuiDS = VK_NULL_HANDLE;
    };
}
