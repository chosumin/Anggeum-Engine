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
    class OcclusionCuller;

    class FGGeometryPass : public FrameGraphPass
    {
    public:
        static constexpr const char* RT_MAIN_COLOR = "MainColor";
        static constexpr const char* RT_MAIN_DEPTH = "MainDepth";
        static constexpr const char* RT_IRRADIANCE = "Irradiance";
        static constexpr const char* RT_PREFILTERED = "Prefiltered";
        static constexpr const char* RT_BRDF_LUT = "BrdfLut";
        static constexpr const char* RT_OFFSCREEN = "Offscreen";

        FGGeometryPass(Device& device, Scene& scene, SwapChain& swapChain,
            VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, ivec2 tileNums);
        ~FGGeometryPass();

        const char* GetName() const override { return "FGGeometryPass"; }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderExecutor& renderExecutor) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

    private:
        void EnsureIBLResources(FrameResources& frameResources);

        // One-time IBL generation: records the pre-environment/BRDF jobs into a
        // single-time command buffer and waits (main thread, startup only).
        void GenerateIBLResources();
        void RegisterGiTexturesToBindless(FrameResources& frameResources,
            RenderExecutor& renderExecutor);

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

        GI _giBuffer;
        TileInfo _tileInfo;

        bool _iblGenerated = false;

        Handle<Texture> _offscreenTexture;
        Handle<Texture> _irradianceCubemap;
        Handle<Texture> _prefilteredCubemap;
        Handle<Texture> _brdfLut;

        // Stashed per frame in Setup, consumed by Execute on the worker.
        FGTexture _mainColor;
        FGTexture _mainDepth;
        Handle<Texture> _shadowTarget;
        Handle<Texture> _sdfShadowTarget;
        Handle<Texture> _aoTarget;
        Buffer* _cameraBuffer = nullptr;
        Buffer* _lightBuffer = nullptr;
        Buffer* _shadowBuffer = nullptr;
        Buffer* _giBufferUB = nullptr;
        Buffer* _lightVisibilityBuffer = nullptr;
        Shader* _geometryShader = nullptr;
        Pipeline* _geometryPipeline = nullptr;
        OcclusionCuller* _culler = nullptr;

        // Skybox stash (found once; scene meshes are static after load)
        Shader* _skyboxShader = nullptr;
        Material* _skyboxMaterial = nullptr;
        SubMesh* _skyboxSubMesh = nullptr;
    };
}
