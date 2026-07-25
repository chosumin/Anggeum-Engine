#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
    class Scene;
    class Pipeline;
    class Material;
    class SubMesh;
    class Texture;
    class Shader;
    class Buffer;

    class PreEnvironmentPass : public RendererPass
    {
    public:
        PreEnvironmentPass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene,
            Texture* offscreen, Texture* irradianceCubemap, Texture* prefilteredCubemap);
        ~PreEnvironmentPass();

        void Initialize();
        void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex) override;

    private:
        void DrawIrradiance(RenderFrame& renderFrame, CommandBuffer& commandBuffer);
        void DrawPrefiltered(RenderFrame& renderFrame, CommandBuffer& commandBuffer);

    private:
        Scene& _scene;

        Texture* _colorRenderTarget;
        Texture* _irradianceCubemap;
        Texture* _prefilteredCubemap;

        // Resolved on the main thread in the ctor; Draw() runs on a worker thread,
        // so it must not resolve a handle through the pool. The pool owns the
        // shaders for the app's lifetime, so these pointers stay valid. The
        // materials are only needed for their shaders, so we keep just those.
        Shader* _irradianceShader = nullptr;
        Shader* _prefilteredShader = nullptr;

        Pipeline* _irradiancePipeline = nullptr;
        Pipeline* _prefilteredPipeline = nullptr;

        // Resolved from a Handle<SubMesh> on the main thread in Initialize(); Draw()
        // runs on a worker thread, so it must not resolve the handle through the pool.
        // The sky's vertex/index buffers are resolved to raw pointers there too (the
        // SubMesh now holds Handle<Buffer>, so resolving in Draw would touch the pool).
        SubMesh* _sky = nullptr;
        vector<Buffer*> _irradianceVertexBuffers;
        vector<Buffer*> _prefilteredVertexBuffers;
        Buffer* _skyIndexBuffer = nullptr;
        VkIndexType _skyIndexType{};
        Handle<Texture> _skyCubemap;

        vector<mat4> _mvpMatrices;
        IrradianceDelta _delta;
        PrefilterEnv _prefilterEnv;

        unique_ptr<Framebuffer> _framebuffer;
    };

    class PreEnvironmentJob : public Job
    {
    public:
        PreEnvironmentJob(Device& device, PreEnvironmentPass& pass);
        ~PreEnvironmentJob();

        void Execute() override;

    private:
        PreEnvironmentPass& _pass;
        RenderFrame _tempRenderFrame;
    };
}
