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

        // RenderExecutor's mid-pass Hi-Z resolve depends on the static shader/
        // pipeline this initializes. Called by the legacy constructor, and by the
        // frame graph pipeline where no ResolvePass instance exists anymore.
        //
        // BRIDGE-ERA: this pair dies with the legacy ResolvePass class. Once
        // GeometryPass migrates (last user of the legacy occlusion overload),
        // move ResolveDepth and its shader/pipeline into the occlusion flow's
        // owner (OcclusionCuller/RenderExecutor) as instance members — instance
        // lifetime then replaces the manual Initialize/Destroy pairing.
        static void InitializeStatics(Device& device, VkExtent2D screenExtent);

        // Must run before the device is destroyed: a static unique_ptr would
        // otherwise destroy the VkPipeline at static-destruction time, after
        // vkDestroyDevice. Called by the legacy destructor and by
        // ForwardRenderPipeline's destructor on the frame graph path.
        static void DestroyStatics();
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
