#pragma once
#include "Culler.h"

namespace Core
{
    class FrustumCuller : public Culler
    {
    public:
        FrustumCuller(Device& device, FrameResources& frameResources,
            RendererBatch& rendererBatch, uint32_t id);

        // The builder must be created for GetCullingShader() so a fresh descriptor
        // set is used per call (multiple cullers share the shader).
        void Dispatch(FrameResources& frameResources, CommandBuffer& commandBuffer,
            DescriptorSetBuilder& builder, const CameraBuffer& camera);

        // Shader used for the culling dispatch (needed to build its descriptor set).
        Shader& GetCullingShader() const;

    private:
        // Culling parameters. Pool-owned by FrameResources (a Culler lives per
        // frame-in-flight: FrameResources -> Culler); held by handle.
        Handle<Buffer> _cullDataBuffer;

        Handle<Shader> _cullingShader;
        Handle<Pipeline> _cullingPipeline;
        Handle<Shader> _resetDrawCommandsShader;
        Handle<Pipeline> _resetDrawCommandsPipeline;
    };
}
