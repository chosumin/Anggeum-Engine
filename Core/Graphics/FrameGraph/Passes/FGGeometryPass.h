#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Device;
    class Scene;
    class SwapChain;
    class Shader;
    class Pipeline;
    class PipelineState;
    class Material;
    class SubMesh;
    class Buffer;

    class FGGeometryPass : public FrameGraphPass
    {
    public:
        static constexpr const char* RT_MAIN_COLOR = "MainColor";
        static constexpr const char* RT_MAIN_DEPTH = "MainDepth";

        FGGeometryPass(Device& device, Scene& scene, SwapChain& swapChain,
            VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, ivec2 tileNums);
        ~FGGeometryPass();

        const char* GetName() const override { return "FGGeometryPass"; }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

    private:
        void PrepareSkybox();
        void RecordSkybox(FrameGraphPassContext& context, CommandBuffer& commandBuffer);

        Pipeline* GetOrCreatePipeline(Shader& shader);

    private:
        Device& _device;
        Scene& _scene;
        VkSampleCountFlagBits _msaaSamples;
        VkFormat _swapChainFormat;
        VkFormat _depthFormat;

        unique_ptr<PipelineState> _pipelineState;
        unique_ptr<Pipeline> _skyboxPipeline;

        // Per-shader pipeline cache
        unordered_map<Shader*, unique_ptr<Pipeline>> _pipelineCache;

        TileInfo _tileInfo;

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
        Pipeline* _geometryPipeline = nullptr;

        // Both culled draw lists, produced by the HiZCull passes.
        FGBuffer _pass1Indirect;
        FGBuffer _pass2Indirect;

        Shader* _skyboxShader = nullptr;
        Material* _skyboxMaterial = nullptr;
        SubMesh* _skyboxSubMesh = nullptr;
    };
}
