#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/Terrain/TerrainPatchCuller.h"
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
    class PerspectiveCamera;
    class Buffer;
    class FrameResources;
    class RendererBatch;
    class HiZPyramid;

    // Two-pass GPU occlusion culling of the mesh draw set:
    //
    // Cull1: cull against the previous frame's final-depth Hi-Z, 
    // then compact the surviving commands for DrawIndexedIndirectCount.
    // Cull2: build this frame's Hi-Z from the pass-1 depth, cull the objects
    //  pass 1 rejected against it and compact its own list.
    class HiZCullPass : public FrameGraphPass
    {
    public:
        enum class Phase { Cull1, Cull2 };

        static constexpr const char* SB_PASS1_COUNTS = "OcclusionCull.Pass1Counts";
        static constexpr const char* SB_PASS1_INSTANCE_IDS = "OcclusionCull.Pass1InstanceIDs";
        static constexpr const char* SB_PASS1_INDIRECT = "OcclusionCull.Pass1Indirect";
        static constexpr const char* SB_PASS1_DRAW_COUNT = "OcclusionCull.Pass1DrawCount";

        static constexpr const char* SB_PASS2_COUNTS = "OcclusionCull.Pass2Counts";
        static constexpr const char* SB_PASS2_INSTANCE_IDS = "OcclusionCull.Pass2InstanceIDs";
        static constexpr const char* SB_PASS2_INDIRECT = "OcclusionCull.Pass2Indirect";
        static constexpr const char* SB_PASS2_DRAW_COUNT = "OcclusionCull.Pass2DrawCount";

        // Transparent (Blend) instances of each phase, declared only while the
        // draw set has any; drawn by TransparentPass after the opaque geometry.
        static constexpr const char* SB_PASS1_TRANSPARENT_COUNTS = "OcclusionCull.Pass1TransparentCounts";
        static constexpr const char* SB_PASS1_TRANSPARENT_INSTANCE_IDS = "OcclusionCull.Pass1TransparentInstanceIDs";
        static constexpr const char* SB_PASS1_TRANSPARENT_INDIRECT = "OcclusionCull.Pass1TransparentIndirect";
        static constexpr const char* SB_PASS1_TRANSPARENT_DRAW_COUNT = "OcclusionCull.Pass1TransparentDrawCount";

        static constexpr const char* SB_PASS2_TRANSPARENT_COUNTS = "OcclusionCull.Pass2TransparentCounts";
        static constexpr const char* SB_PASS2_TRANSPARENT_INSTANCE_IDS = "OcclusionCull.Pass2TransparentInstanceIDs";
        static constexpr const char* SB_PASS2_TRANSPARENT_INDIRECT = "OcclusionCull.Pass2TransparentIndirect";
        static constexpr const char* SB_PASS2_TRANSPARENT_DRAW_COUNT = "OcclusionCull.Pass2TransparentDrawCount";

        static constexpr const char* SB_REJECTED_INDICES = "OcclusionCull.RejectedIndices";
        static constexpr const char* SB_REJECTED_COUNT = "OcclusionCull.RejectedCount";

        // FrameResources render target: this frame's pass-1 depth pyramid.
        static constexpr const char* RT_HIZ = "OcclusionCull.HiZ";

        static constexpr const char* SB_TERRAIN_PASS2_PATCH_LIST = "Terrain.Pass2PatchList";
        static constexpr const char* SB_TERRAIN_PASS2_DRAW_ARGS = "Terrain.Pass2DrawArgs";

        // Cull2 takes the Cull1 instance to share its CPU state.
        HiZCullPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
            TerrainPatchCuller& terrainCuller, VkExtent2D screenExtent, Phase phase,
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
        // Owned by the Cull1 instance, shared with Cull2.
        struct SharedState
        {
            CameraBuffer camera{};
            unique_ptr<HiZPyramid> pyramid;
        };

        void SetupMeshes(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame);
        void SetupTerrain(FrameGraphBuilder& builder, FrameResources& frameResources,
            const PerspectiveCamera& camera);

        void CullMeshes(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            RendererBatch& batch);
        void CullTerrain(FrameGraphPassContext& context, CommandBuffer& commandBuffer);
        // One list: appends the commands whose `counts` entry is non-zero.
        void CompactDrawCommands(FrameGraphPassContext& context,
            CommandBuffer& commandBuffer, RendererBatch& batch,
            FGBuffer counts, FGBuffer outCommands, FGBuffer outDrawCount);

        Device& _device;
        ResourceManager& _resourceManager;
        RenderScene& _renderScene;
        TerrainPatchCuller& _terrainCuller;
        Phase _phase;

        // Created by the Cull1 instance, shared by reference with Cull2 — both
        // phases read and write the same camera state.
        shared_ptr<SharedState> _state;

        // Cull dispatch of this phase (gpuCulling / gpuCullingPass2).
        Handle<Shader> _cullShader;
        Handle<Pipeline> _cullPipeline;

        // Appends surviving commands for DrawIndexedIndirectCount.
        Handle<Shader> _compactShader;
        Handle<Pipeline> _compactPipeline;

        // Per-frame scratch, set in Setup and consumed by the same frame's
        // Execute. _active gates Execute entirely (no camera, nothing to cull).
        bool _active = false;
        bool _meshActive = false;
        bool _terrainActive = false;
        Handle<Buffer> _cullData;
        Handle<Texture> _hiZTexture;  // this slot's pass-1 pyramid (Cull2 builds it)
        Handle<Texture> _sampledHiZ;
        FGTexture _resolvedDepth;     // Cull2: this frame's depth, from ResolvePass
        FGBuffer _counts;             // this phase's per-command instance counts
        FGBuffer _instanceIDs;        // this phase's ID scatter target
        FGBuffer _visibleCommands;    // this phase's compacted draw list
        FGBuffer _visibleDrawCount;
        FGBuffer _rejectedIndices;
        FGBuffer _rejectedCount;

        // The transparent counterparts; invalid while the set has no Blend instance.
        FGBuffer _transparentCounts;
        FGBuffer _transparentInstanceIDs;
        FGBuffer _transparentCommands;
        FGBuffer _transparentDrawCount;

        // Terrain pass-2 cull (Cull2 only).
        TerrainPatchCuller::Output _terrainOutput;
        FGBuffer _terrainRejected, _terrainRejectedCount;
        Handle<Buffer> _terrainCullData;
        TerrainTraversalPush _terrainPush{};
    };
}
