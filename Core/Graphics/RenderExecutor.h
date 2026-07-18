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
    class RenderPass;
    class Framebuffer;
    class DescriptorSetBuilder;
    class Scene;

    // Key for culler cache: combination of camera pointer and batch pointer
    struct CullerKey
    {
        const CameraBuffer* Camera;
        RendererBatch* Batch;

        bool operator==(const CullerKey& other) const
        {
            return Camera == other.Camera && Batch == other.Batch;
        }
    };

    struct CullerKeyHash
    {
        size_t operator()(const CullerKey& key) const
        {
            size_t h = 0;
            h ^= std::hash<const CameraBuffer*>{}(key.Camera);
            h ^= std::hash<RendererBatch*>{}(key.Batch) << 1;
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

        // Batch initialization - called once at the start of rendering
        void InitializeBatches(Scene& scene, VkExtent2D extents);
        bool IsBatchesInitialized() const { return _batchesInitialized; }

        // RendererBatch access (for data queries like GetObjectDataBuffer)
        RendererBatch* GetRendererBatch() const { return _rendererBatch.get(); }

        // Two-pass occlusion culling + draw
        void OcclusionCullAndDraw(CommandBuffer& commandBuffer,
            Shader& shader, Pipeline& pipeline,
            CameraBuffer& camera,
            Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
            Framebuffer& framebuffer,
            DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook,
            function<void()> postDraw);

        // Frustum-only culling + draw (e.g. shadow passes)
        void FrustumCullAndDraw(CommandBuffer& commandBuffer,
            Core::RenderPass& renderPass,
            Framebuffer& framebuffer,
            Shader& shader,
            Pipeline& pipeline,
            DescriptorSetBuilder& builder,
            const CameraBuffer& camera, function<void(Shader&)> perShaderHook);

        // Reset per-frame state (culler usage tracking)
        void ResetFrame();

    private:
        Culler* GetOrCreateCuller(RendererBatch* batch, const CameraBuffer& camera, TransformBatch& transformBatch);

        void DrawIndirectInternal(CommandBuffer& commandBuffer,
            Shader& shader, Pipeline& pipeline,
            Core::Buffer& indirectCommandBuffer,
            DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook);

    private:
        Device& _device;
        RenderFrame& _renderFrame;

        // Per-camera/RendererBatch cullers, reused within a frame
        unordered_map<CullerKey, unique_ptr<Culler>, CullerKeyHash> _cullers;

        // Single RendererBatch for all meshes
        unique_ptr<RendererBatch> _rendererBatch;
        TransformBatch _transformBatch{};
        bool _batchesInitialized = false;
    };
}
