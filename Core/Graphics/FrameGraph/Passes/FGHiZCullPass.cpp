#include "stdafx.h"
#include "FGHiZCullPass.h"
#include "FGDepthPrePass.h"
#include "FGResolvePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/OcclusionCuller.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Texture.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

FGHiZCullPass::FGHiZCullPass(Device& device, Scene& scene, Phase phase)
    : _device(device)
    , _scene(scene)
    , _phase(phase)
{
}

FGHiZCullPass::~FGHiZCullPass() = default;

void FGHiZCullPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderFrame& renderFrame)
{
    _culler = nullptr;

    PerspectiveCamera* camera = _scene.GetMainCamera();
    if (!camera)
        return; // nothing to cull: the pass culls itself this frame

    // Same camera as the depth prepass, so this returns the shared culler.
    _culler = renderFrame.PrepareOcclusionCuller(camera->Matrices);
    if (_culler == nullptr)
        return;

    // The culling dispatches write the culler's indirect draw buffers (external
    // to the graph), so the pass must never be culled.
    builder.SetSideEffect();

    if (_phase == Phase::Cull1)
    {
        // Pass-1 Hi-Z reprojects the previous frame's resolved depth (a different
        // frame slot's image); the culler barriers it itself, so it stays outside
        // the graph.
        _prevDepth = frameResources.GetPreviousDepthBuffer();
    }
    else
    {
        // Pass-2 Hi-Z is built from the depth DepthPre1 drew, resolved by the
        // preceding FGResolvePass. The declared read leaves it in SHADER_READ,
        // which is where the Hi-Z build expects to find it.
        _resolvedDepth = builder.GetTexture(FGResolvePass::RT_RESOLVED_DEPTH);
        builder.Read(_resolvedDepth, TextureAccess::SampledCompute);
    }
}

void FGHiZCullPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    if (_culler == nullptr)
        return;

    auto& frameResources = context.GetRenderFrame().GetResources();

    if (_phase == Phase::Cull1)
    {
        // Null on the frames before any depth has been produced.
        _culler->CullPass1(frameResources, commandBuffer, _prevDepth.TryGet());
        return;
    }

    _culler->CullPass2(frameResources, commandBuffer, context.GetTexture(_resolvedDepth));
}
