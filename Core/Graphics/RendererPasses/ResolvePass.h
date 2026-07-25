#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Scene;

    class ResolvePass : public RendererPass
    {
    public:
        static constexpr const char* RT_RESOLVED_DEPTH  = "ResolvedDepth";
        static constexpr const char* RT_RESOLVED_NORMAL = "ResolvedNormal";

        ResolvePass(Device& device, WorkerThreadManager& workerThreadManager,
            VkExtent2D screenExtent, VkSampleCountFlagBits msaaSamples);
        ~ResolvePass();

        void EnsureRenderTargets(RenderFrame& renderFrame) override;
        void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex) override;

        static Handle<Texture> ResolveDepth(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            Handle<Texture> msaaDepth, QueueType destQueue = QueueType::None);
    private:
        void ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            Handle<Texture> msaaNormal);
        
        static Handle<Texture> GetResolvedDepthTarget(RenderFrame& renderFrame);
    private:
        VkSampleCountFlagBits _msaaSamples;

        Handle<Texture> _resolvedDepthTexture;
        Handle<Texture> _resolvedNormalTexture;

        Handle<Shader> _normalResolveShader;
        unique_ptr<Pipeline> _normalResolvePipeline;

        static VkExtent2D _screenExtent;
        static Handle<Shader> _depthResolveShader;
        static unique_ptr<Pipeline> _depthResolvePipeline;
    };
}
