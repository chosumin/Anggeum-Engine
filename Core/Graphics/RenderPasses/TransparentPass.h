#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Device;
    class RenderScene;
    class SwapChain;
    class Shader;
    class Pipeline;
    class PipelineState;

    class TransparentPass : public FrameGraphPass
    {
    public:
        TransparentPass(Device& device, RenderScene& renderScene, SwapChain& swapChain,
            VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, ivec2 tileNums);
        ~TransparentPass();

        const char* GetName() const override { return "TransparentPass"; }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

    private:
        Pipeline* GetOrCreatePipeline(Shader& shader);

    private:
        Device& _device;
        RenderScene& _renderScene;
        VkSampleCountFlagBits _msaaSamples;
        VkFormat _colorFormat;
        VkFormat _depthFormat;

        unique_ptr<PipelineState> _pipelineState;

        // Per-shader pipeline cache
        unordered_map<Shader*, unique_ptr<Pipeline>> _pipelineCache;

        TileInfo _tileInfo;

        // Set in Setup for the same frame's Execute; false when there is no
        // transparent list to draw.
        bool _active = false;

        FGTexture _mainColor;
        FGTexture _mainDepth;
        FGTexture _shadow;
        FGTexture _sdfShadow;
        FGTexture _ao;
        FGBuffer _camera;
        FGBuffer _lights;
        FGBuffer _shadowUB;
        FGBuffer _gi;
        FGBuffer _lightVisibility;
        Shader* _geometryShader = nullptr;
        Pipeline* _pipeline = nullptr;

        // Both compacted transparent lists, produced by the HiZCull passes.
        FGBuffer _pass1Indirect, _pass1Count, _pass1InstanceIDs;
        FGBuffer _pass2Indirect, _pass2Count, _pass2InstanceIDs;
    };
}
