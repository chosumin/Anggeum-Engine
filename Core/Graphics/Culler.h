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

    // GPU-driven culling helper.
    // Owns the compute resources used for frustum/occlusion culling (culling
    // pipelines, the Hi-Z pyramid and the 2-pass buffers) and dispatches the
    // culling passes that populate the indirect draw command buffers created by
    // RendererBatch. The draw buffers themselves stay owned by RendererBatch
    // and are only referenced here.
    class Culler
    {
    public:
        // Builds every culling resource up front. The batch supplies the geometry
        // being culled and must outlive this Culler.
        Culler(Device& device, RendererBatch& rendererBatch);
        ~Culler();

        bool IsUsedThisFrame() const { return _markUsedThisFrame; }
		void MarkUsedThisFrame(bool used) { _markUsedThisFrame = used; }

        // Resets per-frame instance counts before the 2-pass culling runs.
        void ResetDrawCommands(RenderFrame& renderFrame, CommandBuffer& commandBuffer);

        // Frustum + occlusion culling into the primary indirect command buffer (Pass 1).
        void DispatchPass1Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, Handle<Texture> depth);

        // Frustum + occlusion culling of the objects rejected by Pass 1 (Pass 2).
        void DispatchPass2Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, Handle<Texture> depth);

        // Frustum-only culling into the primary indirect command buffer.
        // The builder must be created for the frustum culling shader so a fresh
        // descriptor set is used per call (multiple cullers share the shader).
        void DispatchFrustumOnlyCulling(RenderFrame& renderFrame,
            CommandBuffer& commandBuffer, DescriptorSetBuilder& builder,
            const CameraBuffer& camera);

        Buffer* GetIndirectCommandBuffer() const { return _indirectCommandBuffer.get(); }
        Buffer* GetPass2IndirectCommandBuffer() const { return _pass2IndirectCommandBuffer.get(); }

        // Shader used for frustum-only culling (needed to build its descriptor set).
        Shader& GetFrustumCullingShader() const;

    private:
        void PrepareCullingResources(Device& device, const IndirectDrawBuffer& indirectDrawBuffer);
        void ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes);

        void PrepareHiZResources(Device& device, VkExtent2D extents);
        void GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer, Handle<Texture> depth);

        void DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
            const CameraBuffer& camera, Handle<Texture> depth,
            Core::Buffer& indirectCommandBuffer, Core::Buffer& cullDataBuffer,
            Shader& cullingShader, Pipeline* cullingPipeline);

    private:
        Device& _device;

        // The geometry being culled. 
        // Owns the object/instance/transform buffers this Culler reads.
        RendererBatch& _rendererBatch;

        // Pass 1 indirect command buffer owned by this Culler so multiple cullers
        // don't overwrite each other.
        unique_ptr<Core::Buffer> _indirectCommandBuffer;
        uint32_t _instanceCount = 0;
        uint32_t _drawCount = 0;

        // Culling pipeline (Pass 1)
        Handle<Shader> _cullingShader;
        unique_ptr<Pipeline> _cullingPipeline;

        // Hi-Z Resources. Pass-temp texture, owned by this Culler's own pool so it
        // can be referred to by handle like every other bound texture.
        ResourcePool<Texture> _texturePool;
        Handle<Texture> _hiZTexture;
        Handle<Shader> _hiZGenerateShader;
        unique_ptr<Pipeline> _hiZPipeline;
        uint32_t _hiZMipLevels = 0;
        VkExtent2D _screenExtent = {};

        bool _hiZInitialized = false;

        // Culling parameters, one buffer per dispatch site. 
        // Owned here because a Culler already lives per
        // frame-in-flight (RenderFrame -> RenderExecutor -> Culler).
        unique_ptr<Core::Buffer> _pass1CullDataBuffer;
        unique_ptr<Core::Buffer> _pass2CullDataBuffer;
        unique_ptr<Core::Buffer> _frustumCullDataBuffer;

        // 2-Pass Resources
        unique_ptr<Core::Buffer> _rejectedIndicesBuffer;
        unique_ptr<Core::Buffer> _rejectedCountBuffer;
        unique_ptr<Core::Buffer> _pass2IndirectCommandBuffer;

        Handle<Shader> _pass2CullingShader;
        unique_ptr<Pipeline> _pass2CullingPipeline;
        Handle<Shader> _resetDrawCommandsShader;
        unique_ptr<Pipeline> _resetDrawCommandsPipeline;

        // Frustum-only culling resources
        Handle<Shader> _frustumCullingShader;
        unique_ptr<Pipeline> _frustumCullingPipeline;
        Handle<Shader> _resetDrawCommandsSimpleShader;
        unique_ptr<Pipeline> _resetDrawCommandsSimplePipeline;

		bool _markUsedThisFrame = false;
    };
}
