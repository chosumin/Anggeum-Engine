#pragma once
#include "IndirectDrawBuffer.h"
#include "Culler.h"
#include "RendererBatch.h"

namespace Core
{
    class RenderFrame;
    class CommandBuffer;
    class Shader;
    class Pipeline;
    class DescriptorSetBuilder;
    class Scene;
    class Texture;
    class FrameGraphPassContext;
    class OcclusionCuller;
    class FrustumCuller;
    class MeshBufferManager;
    class BindlessTextureManager;

    // Key for the culler cache. The concrete type is part of the key so the same
    // camera can own.
    struct CullerKey
    {
        const CameraBuffer* Camera;
        RendererBatch* Batch;
        type_index Type;

        bool operator==(const CullerKey& other) const
        {
            return Camera == other.Camera && Batch == other.Batch && Type == other.Type;
        }
    };

    struct CullerKeyHash
    {
        size_t operator()(const CullerKey& key) const
        {
            size_t h = 0;
            h ^= std::hash<const CameraBuffer*>{}(key.Camera);
            h ^= std::hash<RendererBatch*>{}(key.Batch) << 1;
            h ^= key.Type.hash_code() << 2;
            return h;
        }
    };

    // RenderExecutor: Orchestrates culling and indirect drawing.
    // Owns cullers and the renderer batch, and provides high-level draw methods.
    class RenderExecutor
    {
    public:
        RenderExecutor(Device& device, RenderFrame& renderFrame);
        ~RenderExecutor();

        // RendererBatch access (for data queries like GetObjectDataBuffer). The batch
        // is owned by RenderContext and shared across frames; forwarded from the frame.
        RendererBatch* GetRendererBatch() const;

        MeshBufferManager& GetMeshBufferManager() const;

        bool HasBindlessSupport() const;
        BindlessTextureManager* GetBindlessTextureManager() const;

        // Two-pass occlusion culling + draw
        void OcclusionCullAndDraw(CommandBuffer& commandBuffer,
            Shader& shader, Pipeline& pipeline,
            OcclusionCuller& culler,
            FrameGraphPassContext& context,
            Texture& colorTarget, Texture& depthTarget,
            DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook,
            function<void()> postDraw);

        OcclusionCuller* PrepareOcclusionCuller(CameraBuffer& camera);
        FrustumCuller* PrepareFrustumCuller(CameraBuffer& camera);

        void FrustumCullAndDraw(CommandBuffer& commandBuffer,
            FrustumCuller& culler,
            Shader& shader,
            Pipeline& pipeline,
            const VkRenderingInfo& renderingInfo,
            DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook);

        void ResetFrame();

    private:
        // Returns the cached culler for (camera, batch, T), creating it on first
        // use. T's constructor must take (Device&, RenderFrame&, RendererBatch&,
        // uint32_t id) — instantiated in the .cpp, where the derived headers are
        // included.
        template<typename T>
        T* GetOrCreateCuller(RendererBatch& batch, const CameraBuffer& camera)
        {
            CullerKey key{ &camera, &batch, type_index(typeid(T)) };
            auto it = _cullers.find(key);
            if (it != _cullers.end())
            {
                // The key carries the concrete type, so this cast cannot be wrong.
                T* culler = static_cast<T*>(it->second.get());

                // The batch is a single shared instance, so its address stays put
                // across a rebuild and the key alone cannot tell us the culler went
                // stale. Its buffers are sized from the draw set, so a rebuild has
                // to be picked up before anything records against them.
                if (culler->GetBatchRevision() != batch.GetRevision())
                    culler->OnBatchRebuilt(_renderFrame);

                return culler;
            }

            auto culler = make_unique<T>(_device, _renderFrame, batch, _nextCullerId++);
            T* result = culler.get();
            _cullers[key] = std::move(culler);
            return result;
        }

        void DrawIndirectInternal(CommandBuffer& commandBuffer,
            Shader& shader, Pipeline& pipeline,
            Core::Buffer& indirectCommandBuffer,
            DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook);

        // Mid-pass Hi-Z input for phase-2 occlusion culling: resolves the MSAA
        // depth (already shader-readable) into the ResolvedDepth target and
        // leaves it shader-readable.
        Handle<Texture> ResolveDepthForCulling(CommandBuffer& commandBuffer,
            Handle<Texture> msaaDepth);

    private:
        Device& _device;
        RenderFrame& _renderFrame;

        // Mid-pass depth resolve (see ResolveDepthForCulling).
        Handle<Shader> _depthResolveShader;
        unique_ptr<Pipeline> _depthResolvePipeline;

        // Per-camera/RendererBatch cullers, reused within a frame
        unordered_map<CullerKey, unique_ptr<Culler>, CullerKeyHash> _cullers;

        // Monotonic index handed to each new Culler so its FrameResources entries
        // get unique names (cullers never removed, so ids stay stable per frame).
        uint32_t _nextCullerId = 0;
    };
}
