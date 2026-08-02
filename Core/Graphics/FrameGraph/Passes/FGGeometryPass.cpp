#include "stdafx.h"
#include "FGGeometryPass.h"
#include "FGDepthPrePass.h"
#include "FGShadowPass.h"
#include "FGSDFShadowPass.h"
#include "FGAmbientOcclusionPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/RenderExecutor.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "Graphics/RendererPasses/PreEnvironmentPass.h"
#include "Graphics/RendererPasses/BrdfLutPass.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Mesh.h"

using namespace Core;

FGGeometryPass::FGGeometryPass(Device& device, Scene& scene, SwapChain& swapChain,
    VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, ivec2 tileNums)
    : _device(device)
    , _scene(scene)
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

FGGeometryPass::~FGGeometryPass() = default;

Pipeline* FGGeometryPass::GetOrCreatePipeline(Shader& shader)
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

void FGGeometryPass::EnsureIBLResources(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderExecutor& renderExecutor)
{
    // One-time: everything below (creation, generators, bindless) is done on the
    // frame the volume is first generated. The textures are app-lifetime and
    // sampled via bindless afterward, so they're only imported/resolved here.
    if (_iblGenerated)
        return;

    // Offscreen (for IBL generation)
    RenderTargetDesc offscreenDesc{};
    offscreenDesc.extent = { 128, 128 };
    offscreenDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    offscreenDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    offscreenDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    offscreenDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    auto offscreenHandle = frameResources.GetOrCreateRenderTarget(RT_OFFSCREEN, offscreenDesc);

    // Irradiance cubemap
    RenderTargetDesc irradianceDesc{};
    irradianceDesc.extent = { 128, 128 };
    irradianceDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    irradianceDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    irradianceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    irradianceDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    irradianceDesc.isCubemap = true;
    irradianceDesc.mipLevels = 8;
    irradianceDesc.arrayLayers = 6;
    auto irradianceHandle = frameResources.GetOrCreateRenderTarget(RT_IRRADIANCE, irradianceDesc);

    // Prefiltered cubemap
    RenderTargetDesc prefilteredDesc{};
    prefilteredDesc.extent = { 128, 128 };
    prefilteredDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    prefilteredDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    prefilteredDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    prefilteredDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    prefilteredDesc.isCubemap = true;
    prefilteredDesc.mipLevels = 8;
    prefilteredDesc.arrayLayers = 6;
    auto prefilteredHandle = frameResources.GetOrCreateRenderTarget(RT_PREFILTERED, prefilteredDesc);

    // BRDF LUT
    RenderTargetDesc brdfLutDesc{};
    brdfLutDesc.extent = { 512, 512 };
    brdfLutDesc.format = VK_FORMAT_R16G16_SFLOAT;
    brdfLutDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    brdfLutDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    brdfLutDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    auto brdfLutHandle = frameResources.GetOrCreateRenderTarget(RT_BRDF_LUT, brdfLutDesc);

    // Import + WriteManual: the generators do their own layout transitions
    // (UNDEFINED discard → attachment/transfer → shader-read), so the graph
    // emits no barriers and only needs them resolvable through the context.
    auto importManual = [&](const char* name, Handle<Texture> handle, TextureAccess finalState)
    {
        FGTexture t = builder.ImportTexture(name, handle,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED);
        builder.WriteManual(t, finalState);
        return t;
    };
    _offscreen = importManual(RT_OFFSCREEN, offscreenHandle, TextureAccess::ColorWrite);
    _irradiance = importManual(RT_IRRADIANCE, irradianceHandle, TextureAccess::SampledFragment);
    _prefiltered = importManual(RT_PREFILTERED, prefilteredHandle, TextureAccess::SampledFragment);
    _brdfLut = importManual(RT_BRDF_LUT, brdfLutHandle, TextureAccess::SampledFragment);

    _preEnvironmentPass = make_unique<PreEnvironmentPass>(_device, _scene, offscreenDesc.format);
    _preEnvironmentPass->Initialize();

    _brdfLutPass = make_unique<BrdfLutPass>(_device, brdfLutDesc.format);
    _brdfLutPass->Initialize();

    RegisterGiTexturesToBindless(renderExecutor, irradianceHandle, prefilteredHandle, brdfLutHandle);

    _iblGenerated = true;
    _recordIBL = true;
}

void FGGeometryPass::RegisterGiTexturesToBindless(RenderExecutor& renderExecutor,
    Handle<Texture> irradiance, Handle<Texture> prefiltered, Handle<Texture> brdfLut)
{
    if (!renderExecutor.HasBindlessSupport())
        return;

    auto* bindlessManager = renderExecutor.GetBindlessTextureManager();

    uint32_t irradianceCubemapIndex = bindlessManager->RegisterTexture(irradiance);
    uint32_t prefilteredCubemapIndex = bindlessManager->RegisterTexture(prefiltered);
    uint32_t brdfLutIndex = bindlessManager->RegisterTexture(brdfLut);

    // Strip the MSB cubemap flag before passing to the shader.
    // The bindless index stores BindlessCubemapFlag as a cubemap marker internally,
    // but the shader uses the value as a direct array index (no flags expected).
    _giBuffer.irradianceMapIndex = irradianceCubemapIndex & ~BindlessCubemapFlag;
    _giBuffer.prefilterMapIndex  = prefilteredCubemapIndex & ~BindlessCubemapFlag;
    _giBuffer.brdfLUTIndex       = brdfLutIndex & ~BindlessCubemapFlag;

    std::cout << "GI textures registered to bindless:" << endl;
    std::cout << "  Irradiance cubemap: index " << _giBuffer.irradianceMapIndex << endl;
    std::cout << "  Prefiltered cubemap: index " << _giBuffer.prefilterMapIndex << endl;
    std::cout << "  BRDF LUT: index " << _giBuffer.brdfLUTIndex << endl;
}

void FGGeometryPass::PrepareSkybox()
{
    if (_skyboxShader != nullptr)
        return;

    auto meshes = _scene.GetComponents<Core::Mesh>();
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

void FGGeometryPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderExecutor& renderExecutor)
{
    _culler = nullptr;

    PerspectiveCamera* camera = _scene.GetMainCamera();
    if (!camera)
        return; // nothing declared: the pass culls itself this frame

    VkExtent2D screenExtent = {
        static_cast<uint32_t>(_tileInfo.viewportSize.x),
        static_cast<uint32_t>(_tileInfo.viewportSize.y)
    };

    // MSAA color target; the legacy GUI pass loads it afterwards and resolves
    // it into the swapchain image, so both entry and export stay ATTACHMENT.
    RenderTargetDesc colorDesc{};
    colorDesc.extent = screenExtent;
    colorDesc.format = _swapChainFormat;
    colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    colorDesc.samples = _msaaSamples;
    colorDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    colorDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    auto colorTexture = frameResources.GetOrCreateRenderTarget(RT_MAIN_COLOR, colorDesc);

    EnsureIBLResources(builder, frameResources, renderExecutor);

    _mainColor = builder.ImportTexture(RT_MAIN_COLOR, colorTexture,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    _mainDepth = builder.GetTexture(RT_MAIN_DEPTH);

    // Variant 0: phase-1 render (color CLEAR, depth LOAD).
    // Variant 1: phase-2 render (color LOAD, depth LOAD).
    FGAttachment color0;
    color0.texture = _mainColor;
    color0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color0.clear.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    builder.SetColorAttachment(0, color0, 0);

    FGAttachment depth0;
    depth0.texture = _mainDepth;
    depth0.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    builder.SetDepthAttachment(depth0, 0);

    FGAttachment color1 = color0;
    color1.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    builder.SetColorAttachment(0, color1, 1);
    builder.SetDepthAttachment(depth0, 1);

    // Inputs produced by the earlier graph passes. The compile derives the
    // graphics<-compute waits from these (replacing the legacy manual
    // semaphore waits and acquire barriers).
    _shadow = builder.GetTexture(FGShadowPass::RT_SHADOW_DEPTH);
    builder.Read(_shadow, TextureAccess::SampledFragment);

    _sdfShadow = FGTexture{};
    if (builder.HasTexture(FGSDFShadowPass::RT_SDF_SHADOW))
    {
        _sdfShadow = builder.GetTexture(FGSDFShadowPass::RT_SDF_SHADOW);
        builder.Read(_sdfShadow, TextureAccess::SampledFragment);
    }

    _ao = builder.GetTexture(FGAmbientOcclusionPass::RT_AO);
    builder.Read(_ao, TextureAccess::SampledFragment);

    _lightVisibility = builder.GetBuffer(SB_LIGHT_VISIBILITY);
    builder.Read(_lightVisibility, BufferAccess::StorageFragmentRead);

    // Two-phase occlusion flow interleaves compute culling and rendering, and
    // it writes external state (indirect draw buffers, Hi-Z pyramid).
    builder.SetManualRendering();
    builder.SetSideEffect();

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

    auto giHandle = frameResources.GetOrCreateUniformBuffer<GI>("GeometryPass.GI");
    giHandle.Get().Update(_giBuffer);
    _gi = builder.ImportBuffer("GeometryPass.GI", giHandle);
    builder.Read(_gi, BufferAccess::UniformFragment);

    // Get a geometry shader for rendering (use first mesh's material shader)
    _geometryShader = nullptr;
    auto meshes = _scene.GetComponents<Core::Mesh>();
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

    // Same camera as the depth pre-pass, so this returns the same culler; its
    // used-this-frame flag makes Execute reuse the already-culled draw lists.
    _culler = renderExecutor.PrepareOcclusionCuller(camera->Matrices);
}

void FGGeometryPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    // One-time IBL generation, recorded ahead of the draws that sample it.
    if (_recordIBL)
    {
        _recordIBL = false;
        commandBuffer.BeginDebugMarker("IBL Generation");
        _preEnvironmentPass->Record(context, commandBuffer,
            context.GetTexture(_offscreen),
            context.GetTexture(_irradiance),
            context.GetTexture(_prefiltered));
        _brdfLutPass->Record(commandBuffer, context.GetTexture(_brdfLut));
        commandBuffer.EndDebugMarker();
    }

    // Nothing to draw this frame (no camera, no shader, or no batch).
    if (_culler == nullptr || _geometryShader == nullptr)
        return;

    commandBuffer.SetViewportAndScissor(context.GetRenderArea(0));

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

    auto perShaderHook = [&](Shader& shader)
    {
        commandBuffer.PushConstants(shader, 0, _tileInfo);
    };

    auto& executor = context.GetRenderExecutor();
    executor.OcclusionCullAndDraw(
        commandBuffer,
        *_geometryShader, *_geometryPipeline,
        *_culler,
        context,
        context.GetTexture(_mainColor), context.GetTexture(_mainDepth),
        builder, perShaderHook,
        [&]() { RecordSkybox(context, commandBuffer); });
}

void FGGeometryPass::RecordSkybox(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    if (_skyboxShader == nullptr)
        return;

    auto skyBuilder0 = context.CreateDescriptorSetBuilder(*_skyboxShader, 0);
    skyBuilder0.SetUniformBuffer(0, context.GetBuffer(_camera));
    auto& skyResources0 = skyBuilder0.Build();

    auto skyBuilder1 = context.CreateDescriptorSetBuilder(*_skyboxShader, 1);
    auto& textures = _skyboxMaterial->GetTexturesMap();
    for (auto& [binding, texture] : textures)
        skyBuilder1.SetTextureBuffer(binding, texture);
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
