#pragma once

namespace Core
{
    class Device;
    class Texture;
    class Material;
    class Pipeline;
    class PipelineState;
    class CommandBuffer;

    // One-time BRDF LUT generator (dynamic rendering): fullscreen triangle into
    // the LUT target.
    class BrdfLutPass
    {
    public:
        BrdfLutPass(Device& device, VkFormat lutFormat);
        ~BrdfLutPass();

        void Initialize();
        void Record(CommandBuffer& commandBuffer, Texture& brdfLut);

    private:
        Device& _device;
        VkFormat _lutFormat;

        unique_ptr<PipelineState> _pipelineState;

        Material* _brdfMaterial = nullptr;
        unique_ptr<Pipeline> _brdfPipeline;
    };
}
