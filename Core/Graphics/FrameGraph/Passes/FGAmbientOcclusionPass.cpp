#include "stdafx.h"
#include "FGAmbientOcclusionPass.h"
#include "FGDepthPrePass.h"
#include "FGResolvePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RendererPasses/CACAOPass.h"
#include "Graphics/RendererPasses/DFAOPass.h"

using namespace Core;

FGAmbientOcclusionPass::FGAmbientOcclusionPass(Device& device, Scene& scene,
    VkExtent2D screenExtent, VkSampleCountFlagBits msaaSamples,
    SDFGenerator* sdfGenerator)
    : _msaaSamples(msaaSamples)
{
    _cacaoPass = make_unique<CACAOPass>(device, scene, screenExtent, msaaSamples);
    _dfaoPass = make_unique<DFAOPass>(device, scene, screenExtent, msaaSamples, sdfGenerator);
}

FGAmbientOcclusionPass::~FGAmbientOcclusionPass() = default;

void FGAmbientOcclusionPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderExecutor& renderExecutor)
{
    switch (_activeMethod)
    {
        case AOMethod::CACAO: 
            _cacaoPass->EnsureRenderTargets(frameResources); 
            break;
        case AOMethod::DFAO:  
            _dfaoPass->EnsureRenderTargets(frameResources); 
            break;
    }

    // Both methods sample the frame's depth and normals, declared by the
    // earlier graph passes (resolved variants when MSAA is on).
    const bool msaa = _msaaSamples != VK_SAMPLE_COUNT_1_BIT;
    const char* depthName = msaa
        ? FGResolvePass::RT_RESOLVED_DEPTH
        : FGDepthPrePass::RT_MAIN_DEPTH;
    const char* normalName = msaa
        ? FGResolvePass::RT_RESOLVED_NORMAL
        : FGDepthPrePass::RT_MAIN_NORMAL;
    builder.Read(builder.GetTexture(depthName), TextureAccess::SampledCompute);
    builder.Read(builder.GetTexture(normalName), TextureAccess::SampledCompute);

    auto depth = frameResources.GetRenderTarget(depthName);
    auto normal = frameResources.GetRenderTarget(normalName);

    FGTexture ao = builder.ImportTexture(RT_AO, frameResources.GetRenderTarget(RT_AO));

    switch (_activeMethod)
    {
        case AOMethod::CACAO:
            // FFX manages the output's layout itself (discards via UNDEFINED, ends
            // at SHADER_READ_ONLY), so the graph only tracks the final state.
            builder.WriteManual(ao, TextureAccess::SampledCompute);
            _ready = _cacaoPass->Prepare(frameResources, depth, normal);
            break;
        case AOMethod::DFAO:
            builder.Write(ao, TextureAccess::StorageComputeWrite);
            _ready = _dfaoPass->Prepare(frameResources, depth, normal);
            break;
    }
}

void FGAmbientOcclusionPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    if (!_ready)
        return;

    switch (_activeMethod)
    {
        case AOMethod::CACAO: 
            _cacaoPass->Record(commandBuffer); 
            break;
        case AOMethod::DFAO: 
            _dfaoPass->Record(context, commandBuffer); 
            break;
    }
}

void FGAmbientOcclusionPass::OnGUI(RenderFrame& renderFrame)
{
    if (!ImGui::CollapsingHeader("Ambient Occlusion"))
        return;

    const char* methods[] = { "CACAO (FFX)", "DFAO (Distance Field)" };
    int current = static_cast<int>(_activeMethod);

    if (ImGui::Combo("AO Method", &current, methods, IM_ARRAYSIZE(methods)))
    {
        SetMethod(static_cast<AOMethod>(current));
    }

    ImGui::Separator();
    ImGui::Spacing();

    switch (_activeMethod)
    {
        case AOMethod::CACAO: 
            _cacaoPass->UpdateGUI(); 
            break;
        case AOMethod::DFAO:  
            _dfaoPass->UpdateGUI(); 
            break;
    }

    ImGui::Separator();
}
