#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class ResourceManager;
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
    // Cull1: cull against the previous frame's Hi-Z, then compact the
    //  surviving commands for DrawIndexedIndirectCount.
    // Cull2: cull the objects pass 1 rejected against this frame's Hi-Z
    //  and compact its own list.
    class HiZCullPass : public FrameGraphPass
    {
    public:
        enum class Phase { Cull1, Cull2 };

        static constexpr const char* SB_PASS1_COUNTS = "OcclusionCull.Pass1Counts";
        static constexpr const char* SB_PASS1_INSTANCE_IDS = "OcclusionCull.Pass1InstanceIDs";
        static constexpr const char* SB_PASS1_INDIRECT = "OcclusionCull.Pass1Indirect";
        static constexpr const char* SB_PASS1_MATERIALS = "OcclusionCull.Pass1Materials";
        static constexpr const char* SB_PASS1_DRAW_COUNT = "OcclusionCull.Pass1DrawCount";

        static constexpr const char* SB_PASS2_COUNTS = "OcclusionCull.Pass2Counts";
        static constexpr const char* SB_PASS2_INSTANCE_IDS = "OcclusionCull.Pass2InstanceIDs";
        static constexpr const char* SB_PASS2_INDIRECT = "OcclusionCull.Pass2Indirect";
        static constexpr const char* SB_PASS2_MATERIALS = "OcclusionCull.Pass2Materials";
        static constexpr const char* SB_PASS2_DRAW_COUNT = "OcclusionCull.Pass2DrawCount";
        
        static constexpr const char* SB_REJECTED_INDICES = "OcclusionCull.RejectedIndices";
        static constexpr const char* SB_REJECTED_COUNT = "OcclusionCull.RejectedCount";

        // FrameResources render target.
        static constexpr const char* RT_HIZ = "OcclusionCull.HiZ";

        // Cull2 takes the Cull1 instance to share its CPU state.
        HiZCullPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
            VkExtent2D screenExtent, Phase phase, HiZCullPass* cull1 = nullptr);
        ~HiZCullPass();

        const char* GetName() const override
        {
            return _phase == Phase::Cull1 ? "HiZCull1" : "HiZCull2";
        }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

    private:
        // Cross-frame CPU state, per frame slot (each slot owns its own Hi-Z image).
        struct SlotState
        {
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

        void EnsureHiZTexture(FrameResources& frameResources);

        void DispatchCulling(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            RendererBatch& batch, SlotState& slot, Texture* depth);
        void CompactDrawCommands(FrameGraphPassContext& context,
            CommandBuffer& commandBuffer, RendererBatch& batch);
        void BuildHiZ(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            Texture& depth);

        Device& _device;
        ResourceManager& _resourceManager;
        RenderScene& _renderScene;
        Phase _phase;

        // Created by the Cull1 instance, shared by reference with Cull2 — both
        // phases read and write the same camera/slot state.
        shared_ptr<SharedState> _state;

        // Cull dispatch of this phase (gpuCulling / gpuCullingPass2).
        Handle<Shader> _cullShader;
        Handle<Pipeline> _cullPipeline;

        // Appends surviving commands for DrawIndexedIndirectCount.
        Handle<Shader> _compactShader;
        Handle<Pipeline> _compactPipeline;

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
        FGBuffer _counts;             // this phase's per-command instance counts
        FGBuffer _instanceIDs;        // this phase's ID scatter target
        FGBuffer _visibleCommands;    // this phase's compacted draw list
        FGBuffer _visibleMaterials;
        FGBuffer _visibleDrawCount;
        FGBuffer _rejectedIndices;
        FGBuffer _rejectedCount;
    };
}
