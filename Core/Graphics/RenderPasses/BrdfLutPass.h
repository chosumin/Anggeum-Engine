#pragma once

namespace Core
{
	class ResourceManager;
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
        BrdfLutPass(Device& device, ResourceManager& resourceManager, VkFormat lutFormat);
        ~BrdfLutPass();

        void Initialize();
        void Record(CommandBuffer& commandBuffer, Texture& brdfLut);

    private:
        Device& _device;
        ResourceManager& _resourceManager;
        VkFormat _lutFormat;

        unique_ptr<PipelineState> _pipelineState;

        Material* _brdfMaterial = nullptr;
        unique_ptr<Pipeline> _brdfPipeline;
    };
}
