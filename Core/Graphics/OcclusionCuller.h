#pragma once
#include "Culler.h"

namespace Core
{
    class OcclusionCuller : public Culler
    {
    public:
        OcclusionCuller(Device& device, FrameResources& frameResources,
            RendererBatch& rendererBatch, uint32_t id);

        // The two halves of 2-pass occlusion culling, one per frame graph pass.
        // Pass 1 resets the draw commands and culls against the previous frame's
        // depth; pass 2 recovers what pass 1 rejected using the depth pass 1 drew.
        // Both take the camera from GetCamera().
        void CullPass1(FrameResources& frameResources, CommandBuffer& commandBuffer,
            Texture* previousDepth);
        void CullPass2(FrameResources& frameResources, CommandBuffer& commandBuffer,
            Texture& currentDepth);

        Buffer* GetPass2IndirectCommandBuffer() const { return &_pass2IndirectCommandBuffer.Get(); }

    protected:
        // Adds the occlusion-only batch-sized buffers (rejected indices, pass 2
        // indirect commands) on top of the base pass-1 buffer.
        void PrepareBatchResources(FrameResources& frameResources) override;

    private:
        // Resets per-frame instance counts before the 2-pass culling runs.
        void ResetDrawCommands(FrameResources& frameResources, CommandBuffer& commandBuffer);

        void PrepareHiZResources(Device& device, FrameResources& frameResources, VkExtent2D extents);
        void GenerateHiZBuffer(FrameResources& frameResources, CommandBuffer& commandBuffer,
            Texture& depth);

        void DispatchCulling(FrameResources& frameResources, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, Texture* depth,
            Core::Buffer& indirectCommandBuffer, Core::Buffer& cullDataBuffer,
            Shader& cullingShader, Pipeline& cullingPipeline);

    private:
        Handle<Shader> _cullingShader;
        Handle<Pipeline> _cullingPipeline;
        Handle<Shader> _pass2CullingShader;
        Handle<Pipeline> _pass2CullingPipeline;
        Handle<Shader> _resetDrawCommandsShader;
        Handle<Pipeline> _resetDrawCommandsPipeline;

        // Hi-Z Resources. Pass-temp texture, pool-owned by FrameResources (like the
        // buffers) and referred to here by handle.
        Handle<Texture> _hiZTexture;
        Handle<Shader> _hiZGenerateShader;
        Handle<Pipeline> _hiZPipeline;
        uint32_t _hiZMipLevels = 0;
        VkExtent2D _screenExtent = {};

        bool _hiZLayoutInitialized = false;
        bool _hiZBuilt = false;

        // Culling parameters, one buffer per dispatch site. Pool-owned by
        // FrameResources (a Culler lives per frame-in-flight:
        // FrameResources -> Culler); held here by handle.
        Handle<Buffer> _pass1CullDataBuffer;
        Handle<Buffer> _pass2CullDataBuffer;

        // 2-Pass Resources
        Handle<Buffer> _rejectedIndicesBuffer;
        Handle<Buffer> _rejectedCountBuffer;
        Handle<Buffer> _pass2IndirectCommandBuffer;
    };
}
