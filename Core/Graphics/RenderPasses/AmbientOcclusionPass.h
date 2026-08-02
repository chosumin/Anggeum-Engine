#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Device;
    class RenderScene;
    class Texture;
    class SDFGenerator;
    class CACAOPass;
    class DFAOPass;

    enum class AOMethod
    {
        CACAO,
        DFAO
    };

    // Ambient occlusion (compute queue): dispatches to the active method's
    // helper (FFX CACAO or distance-field AO), both writing the shared AOResult
    // mask that GeometryPass samples.
    class AmbientOcclusionPass : public FrameGraphPass
    {
    public:
        static constexpr const char* RT_AO = "AOResult";

        AmbientOcclusionPass(Device& device, RenderScene& renderScene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples,
            SDFGenerator* sdfGenerator);
        ~AmbientOcclusionPass();

        const char* GetName() const override { return "AmbientOcclusionPass"; }
        QueueType GetQueueType() const override { return QueueType::Compute; }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;
        void OnGUI(RenderFrame& renderFrame) override;

        void SetMethod(AOMethod method) { _activeMethod = method; }
        AOMethod GetMethod() const { return _activeMethod; }

    private:
        VkSampleCountFlagBits _msaaSamples;

        unique_ptr<CACAOPass> _cacaoPass;
        unique_ptr<DFAOPass> _dfaoPass;
        AOMethod _activeMethod = AOMethod::CACAO;

        bool _ready = false;
        FGTexture _depth;
        FGTexture _normal;
    };
}
