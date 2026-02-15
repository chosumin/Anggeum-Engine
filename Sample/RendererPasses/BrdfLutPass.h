#pragma once
#include "Graphics/RendererPass.h"

namespace Core
{
    class Material;
    class Pipeline;

    class BrdfLutPass : public RendererPass
    {
    public:
        BrdfLutPass(Device& device, WorkerThreadManager& workerThreadManager, Texture* brdfLut);
        ~BrdfLutPass();

        void Prepare() override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

    private:
        unique_ptr<Framebuffer> _framebuffer;

        Material* _brdfMaterial = nullptr;
        Pipeline* _brdfPipeline = nullptr;
    };

    class BrdfLutJob : public Job
    {
    public:
        BrdfLutJob(Device& device, BrdfLutPass& pass);
        ~BrdfLutJob();

        void Execute() override;

    private:
        BrdfLutPass& _pass;
        RenderFrame _tempRenderFrame;
    };
}