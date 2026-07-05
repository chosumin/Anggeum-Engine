#pragma once
#include "IndirectDrawBuffer.h"
#include "BufferObjects.h"

namespace Core
{
    class Device;
    class Buffer;
    class Texture;
    class Shader;
    class Pipeline;
    class CommandBuffer;
    class RenderFrame;
    struct TransformBatch;

    // GPU-driven culling helper.
    // Owns the compute resources used for frustum/occlusion culling (culling
    // pipelines, the Hi-Z pyramid and the 2-pass buffers) and dispatches the
    // culling passes that populate the indirect draw command buffers created by
    // RendererBatches. The draw buffers themselves stay owned by RendererBatches
    // and are only referenced here.
    class Culler
    {
    public:
        Culler(Device& device, TransformBatch& transformBatch);
        ~Culler();

        bool IsUsedThisFrame() const { return _markUsedThisFrame; }
		void MarkUsedThisFrame(bool used) { _markUsedThisFrame = used; }

        // Creates the culling compute resources. The draw buffers are owned by
        // RendererBatches and only referenced by the Culler.
        void Prepare(Device& device, VkExtent2D extents,
            Buffer* objectDataBuffer, Buffer* instanceBuffer,
            Buffer* indirectCommandBuffer, uint32_t instanceCount,
            const IndirectDrawBuffer& indirectDrawBuffer);

        // Resets per-frame instance counts before the 2-pass culling runs.
        void ResetDrawCommands(RenderFrame& renderFrame, CommandBuffer& commandBuffer);

        // Frustum + occlusion culling into the primary indirect command buffer (Pass 1).
        void DispatchPass1Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, shared_ptr<Texture> depth);

        // Frustum + occlusion culling of the objects rejected by Pass 1 (Pass 2).
        void DispatchPass2Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, shared_ptr<Texture> depth);

        // Frustum-only culling into the primary indirect command buffer.
        void DispatchFrustumOnlyCulling(RenderFrame& renderFrame,
            CommandBuffer& commandBuffer, const CameraBuffer& camera);

        Buffer* GetPass2IndirectCommandBuffer() const { return _pass2IndirectCommandBuffer; }

        // Check if the culler has been prepared
        bool IsPrepared() const { return _drawCount > 0; }

    private:
        void PrepareCullingResources(Device& device, const IndirectDrawBuffer& indirectDrawBuffer);
        void ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes);

        void PrepareHiZResources(Device& device, VkExtent2D extents);
        void GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer, shared_ptr<Texture> depth);

        void DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, shared_ptr<Texture> depth,
            Core::Buffer* indirectCommandBuffer,
            shared_ptr<Shader> cullingShader, Pipeline* cullingPipeline);

    private:
        Device& _device;
        TransformBatch& _transformBatch;

        // Draw buffers owned by RendererBatches (referenced, not owned).
        Core::Buffer* _objectDataBuffer = nullptr;
        Core::Buffer* _instanceBuffer = nullptr;
        Core::Buffer* _indirectCommandBuffer = nullptr;
        uint32_t _instanceCount = 0;
        uint32_t _drawCount = 0;

        // Culling pipeline (Pass 1)
        shared_ptr<Shader> _cullingShader;
        unique_ptr<Pipeline> _cullingPipeline;

        // Hi-Z Resources
        shared_ptr<Texture> _hiZTexture;
        shared_ptr<Shader> _hiZGenerateShader = nullptr;
        unique_ptr<Pipeline> _hiZPipeline;
        uint32_t _hiZMipLevels = 0;
        VkExtent2D _screenExtent = {};

        bool _hiZInitialized = false;

        // 2-Pass Resources
        Core::Buffer* _rejectedIndicesBuffer = nullptr;
        Core::Buffer* _rejectedCountBuffer = nullptr;
        Core::Buffer* _pass2IndirectCommandBuffer = nullptr;

        shared_ptr<Shader> _pass2CullingShader;
        unique_ptr<Pipeline> _pass2CullingPipeline;
        shared_ptr<Shader> _resetDrawCommandsShader;
        unique_ptr<Pipeline> _resetDrawCommandsPipeline;

        // Frustum-only culling resources
        shared_ptr<Shader> _frustumCullingShader;
        unique_ptr<Pipeline> _frustumCullingPipeline;
        shared_ptr<Shader> _resetDrawCommandsSimpleShader;
        unique_ptr<Pipeline> _resetDrawCommandsSimplePipeline;

		bool _markUsedThisFrame = false;
    };
}
