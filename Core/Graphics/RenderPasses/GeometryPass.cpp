#include "stdafx.h"
#include "GeometryPass.h"
#include "DepthPrePass.h"
#include "ShadowPass.h"
#include "SDFShadowPass.h"
#include "AmbientOcclusionPass.h"
#include "IBLPass.h"
#include "HiZCullPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
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

GeometryPass::GeometryPass(Device& device, RenderScene& renderScene, SwapChain& swapChain,
    VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, ivec2 tileNums)
    : _device(device)
    , _renderScene(renderScene)
    , _msaaSamples(msaaSamples)
    , _swapChainFormat(swapChain.GetImageFormat())
    , _depthFormat(depthFormat)
{
    auto swapChainExtents = swapChain.GetSwapChainExtent();
    _tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
    _tileInfo.tileNums = tileNums;

    _pipelineState = make_unique<PipelineState>();
    _pipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;

    // The depth pre-pass already wrote final depth; geometry only tests it.
    _pipelineState->GetDepthStencilStateCreateInfo().depthWriteEnable = VK_FALSE;
}

GeometryPass::~GeometryPass() = default;

Pipeline* GeometryPass::GetOrCreatePipeline(Shader& shader)
{
    auto it = _pipelineCache.find(&shader);
    if (it != _pipelineCache.end())
        return it->second.get();

    PipelineRenderingDesc renderingDesc;
    renderingDesc.colorFormats = { _swapChainFormat };
    renderingDesc.depthFormat = _depthFormat;

    auto pipeline = make_unique<Pipeline>(_device, renderingDesc, shader, *_pipelineState);
    Pipeline* result = pipeline.get();
    _pipelineCache[&shader] = std::move(pipeline);
    return result;
}

void GeometryPass::PrepareSkybox()
{
    if (_skyboxShader != nullptr)
        return;

    auto meshes = _renderScene.GetScene().GetComponents<Core::Mesh>();
    auto it = find_if(meshes.begin(), meshes.end(), [](Mesh* mesh)
    {
        auto& material = mesh->GetMaterials()[0].Get();
        auto& shader = material.GetShaderHandle().Get();
        return shader.GetPass() == "Skybox";
    });

    if (it == meshes.end())
        return;

    auto skybox = *it;
    _skyboxMaterial = &skybox->GetMaterials()[0].Get();
    _skyboxSubMesh = &skybox->GetSubMeshes()[0].Get();
    _skyboxShader = &_skyboxMaterial->GetShaderHandle().Get();

    auto pipelineState = *_pipelineState;
    pipelineState.GetDepthStencilStateCreateInfo().depthWriteEnable = VK_FALSE;
    pipelineState.GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_FRONT_BIT;

    PipelineRenderingDesc renderingDesc;
    renderingDesc.colorFormats = { _swapChainFormat };
    renderingDesc.depthFormat = _depthFormat;
    _skyboxPipeline = make_unique<Pipeline>(_device, renderingDesc, *_skyboxShader, pipelineState);
}

void GeometryPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderFrame& renderFrame)
{
    _pass1Indirect = FGBuffer{};
    _pass2Indirect = FGBuffer{};

    PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
    if (!camera)
        return; // nothing declared: the pass culls itself this frame

    VkExtent2D screenExtent = {
        static_cast<uint32_t>(_tileInfo.viewportSize.x),
        static_cast<uint32_t>(_tileInfo.viewportSize.y)
    };

    FGTextureDesc colorDesc{};
    colorDesc.extent = screenExtent;
    colorDesc.format = _swapChainFormat;
    colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    colorDesc.samples = _msaaSamples;
    colorDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    _mainColor = builder.CreateTexture(RT_MAIN_COLOR, colorDesc);

    _mainDepth = builder.GetTexture(RT_MAIN_DEPTH);

    FGAttachment color;
    color.texture = _mainColor;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clear.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    builder.SetColorAttachment(0, color);

    FGAttachment depth;
    depth.texture = _mainDepth;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    builder.SetDepthAttachment(depth);

    // Inputs produced by the earlier graph passes. The compile derives the
    // graphics<-compute waits from these (replacing the legacy manual
    // semaphore waits and acquire barriers).
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

    _lightVisibility = builder.GetBuffer(SB_LIGHT_VISIBILITY);
    builder.Read(_lightVisibility, BufferAccess::StorageFragmentRead);

    // CPU-written frame inputs, imported so Execute can resolve them through
    // the context like every other resource.
    _camera = builder.ImportBuffer(UB_CAMERA,
        frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
    builder.Read(_camera, BufferAccess::UniformVertex);

    _lights = builder.ImportBuffer(UB_LIGHTS,
        frameResources.GetOrCreateUniformBuffer<LightBuffer>(UB_LIGHTS));
    builder.Read(_lights, BufferAccess::UniformFragment);

    _shadowUB = builder.ImportBuffer(UB_SHADOW,
        frameResources.GetOrCreateUniformBuffer<ShadowUniform>(UB_SHADOW));
    builder.Read(_shadowUB, BufferAccess::UniformFragment);

    // Produced by IBLPass (bindless indices of the IBL maps).
    _gi = builder.ImportBuffer(IBLPass::UB_GI,
        frameResources.GetOrCreateUniformBuffer<GI>(IBLPass::UB_GI));
    builder.Read(_gi, BufferAccess::UniformFragment);

    // Get a geometry shader for rendering (use first mesh's material shader)
    _geometryShader = nullptr;
    auto meshes = _renderScene.GetScene().GetComponents<Core::Mesh>();
    for (auto* mesh : meshes)
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

    _geometryPipeline = GetOrCreatePipeline(*_geometryShader);

    PrepareSkybox();

    if (builder.HasBuffer(HiZCullPass::SB_PASS1_INDIRECT))
    {
        _pass1Indirect = builder.GetBuffer(HiZCullPass::SB_PASS1_INDIRECT);
        builder.Read(_pass1Indirect, BufferAccess::IndirectRead);

        _pass2Indirect = builder.GetBuffer(HiZCullPass::SB_PASS2_INDIRECT);
        builder.Read(_pass2Indirect, BufferAccess::IndirectRead);
    }
}

void GeometryPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    // Entered even with nothing to draw (no camera, no shader, or no batch), so
    // the declared loadOp still clears the colour target the GUI pass composites
    // onto.
    context.BeginRendering(commandBuffer);

    if (!_pass1Indirect.IsValid() || _geometryShader == nullptr)
    {
        context.EndRendering(commandBuffer);
        return;
    }

    commandBuffer.SetViewportAndScissor(context.GetRenderArea());

    auto builder = context.CreateDescriptorSetBuilder(*_geometryShader, 0);
    builder.SetUniformBuffer(0, context.GetBuffer(_camera));
    builder.SetUniformBuffer(3, context.GetBuffer(_gi));
    builder.SetUniformBuffer(4, context.GetBuffer(_shadowUB));
    builder.SetUniformBuffer(5, context.GetBuffer(_lights));
    builder.SetStorageBuffer(6, context.GetBuffer(_lightVisibility));
    builder.SetTextureBuffer(7, context.GetTexture(_shadow));

    if (_sdfShadow.IsValid())
        builder.SetTextureBuffer(10, context.GetTexture(_sdfShadow));

    builder.SetTextureBuffer(11, context.GetTexture(_ao));

    commandBuffer.PushConstants(*_geometryShader, 0, _tileInfo);

    // Replay both culled draw lists, then the skybox, all in one scope.
    _renderScene.DrawIndirect(commandBuffer, *_geometryShader, *_geometryPipeline,
        context.GetBuffer(_pass1Indirect), builder);
    _renderScene.DrawIndirect(commandBuffer, *_geometryShader, *_geometryPipeline,
        context.GetBuffer(_pass2Indirect), builder);

    RecordSkybox(context, commandBuffer);

    context.EndRendering(commandBuffer);
}

void GeometryPass::RecordSkybox(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    if (_skyboxShader == nullptr)
        return;

    auto skyBuilder0 = context.CreateDescriptorSetBuilder(*_skyboxShader, 0);
    skyBuilder0.SetUniformBuffer(0, context.GetBuffer(_camera));
    auto& skyResources0 = skyBuilder0.Build();

    auto skyBuilder1 = context.CreateDescriptorSetBuilder(*_skyboxShader, 1);
    auto& textures = _skyboxMaterial->GetTexturesMap();
    for (auto& [binding, texture] : textures)
        skyBuilder1.SetTextureBuffer(binding, texture.Get());
    auto& skyResources1 = skyBuilder1.Build();

    commandBuffer.BindPipeline(_skyboxPipeline.get());

    commandBuffer.BindDescriptorSets(
        _skyboxPipeline->GetPipelineBindPoint(),
        *_skyboxShader, { &skyResources0, &skyResources1 });

    auto vertexAttibuteNames = _skyboxShader->GetVertexAttirbuteNames();

    commandBuffer.BindVertexBuffers(_skyboxSubMesh->GetVertexBuffers(vertexAttibuteNames), 0);

    commandBuffer.BindIndexBuffer(_skyboxSubMesh->GetIndexBuffer(), _skyboxSubMesh->GetIndexType());

    commandBuffer.DrawIndexed(_skyboxSubMesh->GetIndexCount(), 1);
}
