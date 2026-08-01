#pragma once
#include "Graphics/RenderFrame.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/Framebuffer.h"
#include "Foundation/Job.h"

namespace Core
{
    class Material;
    class Pipeline;
    class PipelineState;

    class BrdfLutPass
    {
    public:
        BrdfLutPass(Device& device, Texture* brdfLut);
        ~BrdfLutPass();

        void Initialize();
        void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex);

    private:
        Device& _device;

        unique_ptr<RenderPass> _renderPass;
        unique_ptr<PipelineState> _pipelineState;
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
