#pragma once
#include "Culler.h"

namespace Core
{
    class OcclusionCuller : public Culler
    {
    public:
        OcclusionCuller(Device& device, RenderFrame& renderFrame,
            RendererBatch& rendererBatch, uint32_t id);

        // Resets per-frame instance counts before the 2-pass culling runs.
        void ResetDrawCommands(RenderFrame& renderFrame, CommandBuffer& commandBuffer);

        // Frustum + occlusion culling into the primary indirect command buffer (Pass 1).
        void DispatchPass1Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, Handle<Texture> depth);

        // Frustum + occlusion culling of the objects rejected by Pass 1 (Pass 2).
        void DispatchPass2Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, Handle<Texture> depth);

        Buffer* GetPass2IndirectCommandBuffer() const { return &_pass2IndirectCommandBuffer.Get(); }

    protected:
        // Adds the occlusion-only batch-sized buffers (rejected indices, pass 2
        // indirect commands) on top of the base pass-1 buffer.
        void PrepareBatchResources(RenderFrame& renderFrame) override;

    private:
        void PrepareHiZResources(Device& device, RenderFrame& renderFrame, VkExtent2D extents);
        void GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            Handle<Texture> depth);

        void DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, Handle<Texture> depth,
            Core::Buffer& indirectCommandBuffer, Core::Buffer& cullDataBuffer,
            Shader& cullingShader, Pipeline* cullingPipeline);

    private:
        // Culling pipelines (Pass 1 / Pass 2 / reset)
        Handle<Shader> _cullingShader;
        unique_ptr<Pipeline> _cullingPipeline;
        Handle<Shader> _pass2CullingShader;
        unique_ptr<Pipeline> _pass2CullingPipeline;
        Handle<Shader> _resetDrawCommandsShader;
        unique_ptr<Pipeline> _resetDrawCommandsPipeline;

        // Hi-Z Resources. Pass-temp texture, pool-owned by FrameResources (like the
        // buffers) and referred to here by handle.
        Handle<Texture> _hiZTexture;
        Handle<Shader> _hiZGenerateShader;
        unique_ptr<Pipeline> _hiZPipeline;
        uint32_t _hiZMipLevels = 0;
        VkExtent2D _screenExtent = {};
        bool _hiZInitialized = false;

        // Culling parameters, one buffer per dispatch site. Pool-owned by
        // FrameResources (a Culler lives per frame-in-flight:
        // RenderFrame -> RenderExecutor -> Culler); held here by handle.
        Handle<Buffer> _pass1CullDataBuffer;
        Handle<Buffer> _pass2CullDataBuffer;

        // 2-Pass Resources
        Handle<Buffer> _rejectedIndicesBuffer;
        Handle<Buffer> _rejectedCountBuffer;
        Handle<Buffer> _pass2IndirectCommandBuffer;
    };
}
