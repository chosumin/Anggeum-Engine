#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
    class Device;
    class Scene;
    class OcclusionCuller;
    class Pipeline;

    class FGHiZCullPass : public FrameGraphPass
    {
    public:
        enum class Phase { Cull1, Cull2 };

        FGHiZCullPass(Device& device, Scene& scene, Phase phase);
        ~FGHiZCullPass();

        const char* GetName() const override
        {
            return _phase == Phase::Cull1 ? "HiZCull1" : "HiZCull2";
        }

        void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
            RenderFrame& renderFrame) override;
        void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

    private:
        Device& _device;
        Scene& _scene;
        Phase _phase;

        // Stashed per frame in Setup.
        OcclusionCuller* _culler = nullptr;
        Handle<Texture> _prevDepth;  // Cull1: previous-frame resolved depth
        FGTexture _resolvedDepth;    // Cull2: this frame's depth, from DepthResolve1
    };
}
