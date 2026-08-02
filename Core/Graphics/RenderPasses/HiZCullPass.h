#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
    class Device;
    class RenderScene;
    class Shader;
    class Pipeline;
    class Texture;
    class Buffer;
    class FrameResources;
    class RendererBatch;

    // Two-pass GPU occlusion culling:
    // 
    // Cull1: reset counts + cull against the previous frame's Hi-Z.
    // Cull2: cull the objects pass 1 rejected against this frame's Hi-Z,
    //  built from the depth DepthPre1 drew (ResolvePass output).
    class HiZCullPass : public FrameGraphPass
    {
    public:
        enum class Phase { Cull1, Cull2 };

        static constexpr const char* SB_PASS1_INDIRECT = "OcclusionCull.Pass1Indirect";
        static constexpr const char* SB_PASS2_INDIRECT = "OcclusionCull.Pass2Indirect";
        static constexpr const char* SB_REJECTED_INDICES = "OcclusionCull.RejectedIndices";
        static constexpr const char* SB_REJECTED_COUNT = "OcclusionCull.RejectedCount";

        // FrameResources render target.
        static constexpr const char* RT_HIZ = "OcclusionCull.HiZ";

        // Cull2 takes the Cull1 instance to share its CPU state.
        HiZCullPass(Device& device, RenderScene& renderScene, Phase phase,
            HiZCullPass* cull1 = nullptr);
        ~HiZCullPass();

        const char* GetName() const override
        {
            return _phase == Phase::Cull1 ? "HiZCull1" : "HiZCull2";
        }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

    private:
        // Cross-frame CPU state, per frame slot (each slot owns its own Hi-Z
        // image and batch-sized buffers).
        struct SlotState
        {
            uint64_t batchRevision = 0;
            bool buffersCreated = false;

            Handle<Texture> hiZImage;
            bool hiZBuilt = false;
        };

        // Owned by the Cull1 instance, shared with Cull2.
        struct SharedState
        {
            CameraBuffer camera{};
            VkExtent2D extent{};
            uint32_t hiZMipLevels = 0;
            unordered_map<FrameResources*, SlotState> slots;
        };

        // (Re)creates the batch-sized buffers for this slot when the draw set
        // changed; hands back the handles for this frame's imports.
        void EnsureBatchBuffers(FrameResources& frameResources, RendererBatch& batch,
            SlotState& slot, Handle<Buffer>& outPass1, Handle<Buffer>& outPass2,
            Handle<Buffer>& outRejectedIndices, Handle<Buffer>& outRejectedCount);
        void EnsureHiZTexture(FrameResources& frameResources, RendererBatch& batch);

        void ResetDrawCommands(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            RendererBatch& batch);
        void DispatchCulling(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            RendererBatch& batch, SlotState& slot, Texture* depth);
        void BuildHiZ(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            Texture& depth);

        Device& _device;
        RenderScene& _renderScene;
        Phase _phase;

        // Created by the Cull1 instance, shared by reference with Cull2 — both
        // phases read and write the same camera/slot state.
        shared_ptr<SharedState> _state;

        // Cull dispatch of this phase (gpuCulling / gpuCullingPass2).
        Handle<Shader> _cullShader;
        Handle<Pipeline> _cullPipeline;

        // Cull1 only.
        Handle<Shader> _resetShader;
        Handle<Pipeline> _resetPipeline;

        // Hi-Z mip chain build (both phases).
        Handle<Shader> _hiZShader;
        Handle<Pipeline> _hiZPipeline;

        // Per-frame scratch, set in Setup and consumed by the same frame's
        // Execute. _active gates Execute entirely (no camera / empty batch).
        bool _active = false;
        Handle<Buffer> _cullData;
        Handle<Texture> _hiZTexture;
        Handle<Texture> _prevDepth;   // Cull1: previous frame's resolved depth
        FGTexture _resolvedDepth;     // Cull2: this frame's depth, from ResolvePass
        FGBuffer _indirect;           // the indirect buffer this phase's dispatch fills
        FGBuffer _pass2Indirect;      // Cull1: reset also clears the pass-2 buffer
        FGBuffer _rejectedIndices;
        FGBuffer _rejectedCount;
    };
}
