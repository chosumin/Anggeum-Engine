#include "stdafx.h"
#include "TransparentPass.h"
#include "OpaquePass.h"
#include "ShadowPass.h"
#include "SDFShadowPass.h"
#include "AmbientOcclusionPass.h"
#include "IBLPass.h"
#include "HiZCullPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/RenderScene.h"
#include "Graphics/Material.h"
#include "Graphics/ResourcePool.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Mesh.h"

using namespace Core;

TransparentPass::TransparentPass(Device& device, RenderScene& renderScene, SwapChain& swapChain,
    VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, ivec2 tileNums)
    : _device(device)
    , _renderScene(renderScene)
    , _msaaSamples(msaaSamples)
    , _colorFormat(swapChain.GetImageFormat())
    , _depthFormat(depthFormat)
{
    auto swapChainExtents = swapChain.GetSwapChainExtent();
    _tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
    _tileInfo.tileNums = tileNums;

    _pipelineState = make_unique<PipelineState>();
    _pipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;

    // Tested against the opaque prepass depth; transparents never write it.
    _pipelineState->GetDepthStencilStateCreateInfo().depthWriteEnable = VK_FALSE;

    _pipelineState->GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_NONE;

    auto& blend = _pipelineState->GetColorBlendAttachmentState();
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
}

TransparentPass::~TransparentPass() = default;

Pipeline* TransparentPass::GetOrCreatePipeline(Shader& shader)
{
    auto it = _pipelineCache.find(&shader);
    if (it != _pipelineCache.end())
        return it->second.get();

    PipelineRenderingDesc renderingDesc;
    renderingDesc.colorFormats = { _colorFormat };
    renderingDesc.depthFormat = _depthFormat;

    auto pipeline = make_unique<Pipeline>(_device, renderingDesc, shader, *_pipelineState);
    Pipeline* result = pipeline.get();
    _pipelineCache[&shader] = std::move(pipeline);
    return result;
}

void TransparentPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderFrame& renderFrame)
{
    _active = false;
    _pass1Indirect = FGBuffer{};
    _pass2Indirect = FGBuffer{};

    if (!_renderScene.GetScene().GetMainCamera())
        return; // nothing declared: the pass culls itself this frame

    // The culls declare the transparent lists only while the draw set holds a
    // Blend instance, so this doubles as "anything to draw".
    if (!builder.HasBuffer(HiZCullPass::SB_PASS1_TRANSPARENT_INDIRECT)
        || !builder.HasTexture(OpaquePass::RT_MAIN_COLOR))
        return;

    // Same shader as the opaque geometry: the material flags pick the alpha.
    _geometryShader = nullptr;
    for (auto* mesh : _renderScene.GetScene().GetComponents<Core::Mesh>())
    {
        for (auto& materialHandle : mesh->GetMaterials())
        {
            auto& materialShader = materialHandle.Get().GetShaderHandle().Get();
            if (materialShader.GetPass() == "Geometry")
            {
                _geometryShader = &materialShader;
                break;
            }
        }
        if (_geometryShader)
            break;
    }
    if (_geometryShader == nullptr)
        return;

    _pipeline = GetOrCreatePipeline(*_geometryShader);

    _mainColor = builder.GetTexture(OpaquePass::RT_MAIN_COLOR);
    _mainDepth = builder.GetTexture(OpaquePass::RT_MAIN_DEPTH);

    FGAttachment color;
    color.texture = _mainColor;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    builder.SetColorAttachment(0, color);

    FGAttachment depth;
    depth.texture = _mainDepth;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    builder.SetDepthAttachment(depth);

    // The lit shader's inputs, the same set the geometry pass reads.
    _shadow = builder.GetTexture(ShadowPass::RT_SHADOW_DEPTH);
    builder.Read(_shadow, TextureAccess::SampledFragment);

    _sdfShadow = FGTexture{};
    if (builder.HasTexture(SDFShadowPass::RT_SDF_SHADOW))
    {
        _sdfShadow = builder.GetTexture(SDFShadowPass::RT_SDF_SHADOW);
        builder.Read(_sdfShadow, TextureAccess::SampledFragment);
    }

    _ao = builder.GetTexture(AmbientOcclusionPass::RT_AO);
    builder.Read(_ao, TextureAccess::SampledFragment);

    _lightVisibility = builder.GetBuffer(SB_LIGHT_VISIBILITY_TRANSPARENT);
    builder.Read(_lightVisibility, BufferAccess::StorageFragmentRead);

    _camera = builder.ImportBuffer(UB_CAMERA,
        frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
    builder.Read(_camera, BufferAccess::UniformVertex);

    _lights = builder.ImportBuffer(UB_LIGHTS,
        frameResources.GetOrCreateUniformBuffer<LightBuffer>(UB_LIGHTS));
    builder.Read(_lights, BufferAccess::UniformFragment);

    _shadowUB = builder.ImportBuffer(UB_SHADOW,
        frameResources.GetOrCreateUniformBuffer<ShadowUniform>(UB_SHADOW));
    builder.Read(_shadowUB, BufferAccess::UniformFragment);

    _gi = builder.ImportBuffer(IBLPass::UB_GI,
        frameResources.GetOrCreateUniformBuffer<GI>(IBLPass::UB_GI));
    builder.Read(_gi, BufferAccess::UniformFragment);

    _pass1Indirect = builder.GetBuffer(HiZCullPass::SB_PASS1_TRANSPARENT_INDIRECT);
    builder.Read(_pass1Indirect, BufferAccess::IndirectRead);
    _pass1Count = builder.GetBuffer(HiZCullPass::SB_PASS1_TRANSPARENT_DRAW_COUNT);
    builder.Read(_pass1Count, BufferAccess::IndirectRead);
    _pass1InstanceIDs = builder.GetBuffer(HiZCullPass::SB_PASS1_TRANSPARENT_INSTANCE_IDS);
    builder.Read(_pass1InstanceIDs, BufferAccess::StorageVertexRead);

    if (builder.HasBuffer(HiZCullPass::SB_PASS2_TRANSPARENT_INDIRECT))
    {
        _pass2Indirect = builder.GetBuffer(HiZCullPass::SB_PASS2_TRANSPARENT_INDIRECT);
        builder.Read(_pass2Indirect, BufferAccess::IndirectRead);
        _pass2Count = builder.GetBuffer(HiZCullPass::SB_PASS2_TRANSPARENT_DRAW_COUNT);
        builder.Read(_pass2Count, BufferAccess::IndirectRead);
        _pass2InstanceIDs = builder.GetBuffer(HiZCullPass::SB_PASS2_TRANSPARENT_INSTANCE_IDS);
        builder.Read(_pass2InstanceIDs, BufferAccess::StorageVertexRead);
    }

    _active = true;
}

void TransparentPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    if (!_active)
        return;

    context.BeginRendering(commandBuffer);
    commandBuffer.SetViewportAndScissor(context.GetRenderArea());

    auto builder = context.CreateDescriptorSetBuilder(*_geometryShader, 0);
    builder.SetUniformBuffer(0, context.GetBuffer(_camera));
    builder.SetUniformBuffer(3, context.GetBuffer(_gi));
    builder.SetUniformBuffer(4, context.GetBuffer(_shadowUB));
    builder.SetUniformBuffer(5, context.GetBuffer(_lights));
    builder.SetStorageBuffer(6, context.GetBuffer(_lightVisibility));
    builder.SetTextureBuffer(7, context.GetTexture(_shadow));

    // No mask yet: the AO map fills the slot, the shader skips the sample.
    builder.SetTextureBuffer(10, context.GetTexture(_sdfShadow.IsValid() ? _sdfShadow : _ao));
    builder.SetTextureBuffer(11, context.GetTexture(_ao));

    commandBuffer.PushConstants(*_geometryShader, 0, _tileInfo);

    _renderScene.DrawIndirect(commandBuffer, *_geometryShader, *_pipeline,
        context.GetBuffer(_pass1Indirect), context.GetBuffer(_pass1Count),
        context.GetBuffer(_pass1InstanceIDs), builder);
    if (_pass2Indirect.IsValid())
    {
        _renderScene.DrawIndirect(commandBuffer, *_geometryShader, *_pipeline,
            context.GetBuffer(_pass2Indirect), context.GetBuffer(_pass2Count),
            context.GetBuffer(_pass2InstanceIDs), builder);
    }

    context.EndRendering(commandBuffer);
}
