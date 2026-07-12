#pragma once
#include "Graphics/RendererPass.h"

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
        void Draw(RenderFrame& renderFrame, SyncContext& syncContext, CommandBuffer& commandBuffer, uint32_t imageIndex) override;

        static shared_ptr<Texture> ResolveDepth(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            shared_ptr<Texture> msaaDepth);
    private:
        void ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            shared_ptr<Texture> msaaNormal);
        
        static shared_ptr<Texture> GetResolvedDepthTarget(RenderFrame& renderFrame);
    private:
        VkSampleCountFlagBits _msaaSamples;

        shared_ptr<Texture> _resolvedDepthTexture;
        shared_ptr<Texture> _resolvedNormalTexture;

        shared_ptr<Shader> _normalResolveShader;
        unique_ptr<Pipeline> _normalResolvePipeline;

        static VkExtent2D _screenExtent;
        static shared_ptr<Shader> _depthResolveShader;
        static unique_ptr<Pipeline> _depthResolvePipeline;
    };
}
