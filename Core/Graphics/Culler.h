#pragma once
#include "IndirectDrawBuffer.h"
#include "BufferObjects.h"
#include "ResourceHandle.h"
#include "ResourcePool.h"

namespace Core
{
    class Device;
    class Buffer;
    class Texture;
    class Shader;
    class Pipeline;
    class CommandBuffer;
    class RenderFrame;
    class DescriptorSetBuilder;
    class RendererBatch;

    // GPU-driven culling base.
    class Culler
    {
    public:
        virtual ~Culler();

        bool IsUsedThisFrame() const { return _markUsedThisFrame; }
        void MarkUsedThisFrame(bool used) { _markUsedThisFrame = used; }

        // Revision of the batch this Culler's buffers and counts were built against.
        uint64_t GetBatchRevision() const { return _batchRevision; }

        // Re-point this Culler at the batch's current contents after the draw set was
        // rebuilt. The per-draw buffers are swapped in place, so handles already
        // handed out stay valid. Must run before anything records against them this
        // frame.
        void OnBatchRebuilt(RenderFrame& renderFrame);

        Buffer* GetIndirectCommandBuffer() const { return &_indirectCommandBuffer.Get(); }

    protected:
        // The batch supplies the geometry being culled and must outlive this Culler.
        // `id` is used to give this Culler's FrameResources entries unique names so cullers don't collide.
        Culler(Device& device, RendererBatch& rendererBatch, uint32_t id);

        // Everything sized from or filled with the batch's draw set. Runs from the
        // derived constructor and again on every rebuild (via OnBatchRebuilt), while
        // the shaders and pipelines around it do not. Overrides extend it with their
        // own batch-sized buffers and must call the base version first.
        virtual void PrepareBatchResources(RenderFrame& renderFrame);

        static void ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes);

    protected:
        Device& _device;

        // The geometry being culled.
        // Owns the object/instance/transform buffers this Culler reads.
        RendererBatch& _rendererBatch;

        // Names this Culler's FrameResources entries. Kept so a rebuild reuses the
        // same names and replaces the buffers instead of leaking new ones.
        string _namePrefix;
        uint64_t _batchRevision = 0;

        // Pass 1 indirect command buffer (one per Culler so multiple cullers don't
        // overwrite each other). Pool-owned by FrameResources; held here by handle.
        Handle<Buffer> _indirectCommandBuffer;
        uint32_t _instanceCount = 0;
        uint32_t _drawCount = 0;

    private:
        bool _markUsedThisFrame = false;
    };
}
