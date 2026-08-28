#pragma once
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class ResourceManager;
    class Device;
    class Scene;
    class Pipeline;
    class PipelineState;
    class Material;
    class SubMesh;
    class Texture;
    class Shader;
    class Buffer;
    class CommandBuffer;
    class FrameGraphPassContext;

    // One-time IBL generator: renders the sky into the
    // offscreen target per cubemap face/mip and copies it into the irradiance
    // and prefiltered cubemaps.
    class PreEnvironmentPass
    {
    public:
        PreEnvironmentPass(Device& device, ResourceManager& resourceManager, Scene& scene, VkFormat offscreenFormat);
        ~PreEnvironmentPass();

        void Initialize();

        void Record(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            Texture& offscreen, Texture& irradiance, Texture& prefiltered);

    private:
        void RecordIrradiance(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            Texture& offscreen, Texture& irradiance);
        void RecordPrefiltered(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
            Texture& offscreen, Texture& prefiltered);

        void BeginOffscreenRendering(CommandBuffer& commandBuffer, VkExtent2D extent,
            Texture& offscreen);

    private:
        Device& _device;
        Scene& _scene;

        unique_ptr<PipelineState> _pipelineState;

        VkFormat _offscreenFormat;

        Shader* _irradianceShader = nullptr;
        Shader* _prefilteredShader = nullptr;

        unique_ptr<Pipeline> _irradiancePipeline;
        unique_ptr<Pipeline> _prefilteredPipeline;

        SubMesh* _sky = nullptr;
        vector<Buffer*> _irradianceVertexBuffers;
        vector<Buffer*> _prefilteredVertexBuffers;
        Buffer* _skyIndexBuffer = nullptr;
        VkIndexType _skyIndexType{};
        Handle<Texture> _skyCubemap;

        vector<mat4> _mvpMatrices;
        IrradianceDelta _delta;
        PrefilterEnv _prefilterEnv;
    };
}
