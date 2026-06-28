#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
    class Scene;
    class Pipeline;
    class Material;
    class SubMesh;
    class Texture;

    class PreEnvironmentPass : public RendererPass
    {
    public:
        PreEnvironmentPass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene,
            Texture* offscreen, Texture* irradianceCubemap, Texture* prefilteredCubemap);
        ~PreEnvironmentPass();

        void Initialize();
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

    private:
        void DrawIrradiance(RenderFrame& renderFrame, CommandBuffer& commandBuffer);
        void DrawPrefiltered(RenderFrame& renderFrame, CommandBuffer& commandBuffer);

    private:
        Scene& _scene;

        Texture* _colorRenderTarget;
        Texture* _irradianceCubemap;
        Texture* _prefilteredCubemap;

        shared_ptr<Material> _irradianceMaterial;
        shared_ptr<Material> _prefilteredMaterial;

        Pipeline* _irradiancePipeline = nullptr;
        Pipeline* _prefilteredPipeline = nullptr;

        shared_ptr<SubMesh> _sky;
        shared_ptr<Texture> _skyCubemap;

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