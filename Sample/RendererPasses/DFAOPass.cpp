#include "stdafx.h"
#include "DFAOPass.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/SDFGenerator.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/ResourceCache.h"
using namespace Core;

DFAOPass::DFAOPass(Device& device, WorkerThreadManager& workerThreadManager,
    Scene& scene, VkExtent2D screenExtent,
    VkSampleCountFlagBits msaaSamples,
    SDFGenerator* sdfGenerator)
    : RendererPass(device, workerThreadManager)
    , _scene(scene)
    , _screenExtent(screenExtent)
    , _msaaSamples(msaaSamples)
    , _sdfGenerator(sdfGenerator)
{
    _dfaoShader   = _device.GetResourceCache().RequestShader("Shaders/dfao.comp.spv");
    _dfaoPipeline = make_unique<Pipeline>(_device, *_dfaoShader);

    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        _normalResolveShader   = _device.GetResourceCache().RequestShader("Shaders/normalResolve.comp.spv");
        _normalResolvePipeline = make_unique<Pipeline>(_device, *_normalResolveShader);
    }
}

DFAOPass::~DFAOPass()
{
    if (_aoImGuiDS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(_aoImGuiDS);
}

void DFAOPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc aoDesc{};
    aoDesc.extent  = { _screenExtent.width / 2, _screenExtent.height / 2 };
    aoDesc.format  = VK_FORMAT_R8_UNORM;
    aoDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    aoDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    aoDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    _aoTexture = renderFrame.GetOrCreateRenderTarget(RT_DFAO, aoDesc);

    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        RenderTargetDesc normalDesc{};
        normalDesc.extent  = _screenExtent;
        normalDesc.format  = VK_FORMAT_R16G16B16A16_SFLOAT;
        normalDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        normalDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        normalDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
        _resolvedNormalTexture = renderFrame.GetOrCreateRenderTarget(RT_NORMAL_RESOLVED, normalDesc);
    }
}

void DFAOPass::ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    shared_ptr<Texture> msaaNormal)
{
    auto& resolvedImage = *_resolvedNormalTexture->GetImage().lock();
    commandBuffer.TransitionImageLayout(resolvedImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    struct PushConstants
    {
        int32_t outputWidth;
        int32_t outputHeight;
        int32_t sampleCount;
        int32_t padding;
    } pc = {
        static_cast<int32_t>(_screenExtent.width),
        static_cast<int32_t>(_screenExtent.height),
        static_cast<int32_t>(_msaaSamples),
        0
    };

    auto builder = renderFrame.CreateDescriptorSetBuilder(*_normalResolveShader, 0);
    builder.SetTextureBuffer(0, msaaNormal);
    builder.SetTextureBuffer(1, _resolvedNormalTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    auto& resources = builder.Build();

    commandBuffer.BindPipeline(_normalResolvePipeline.get());
    commandBuffer.BindDescriptorSet(renderFrame,
        VK_PIPELINE_BIND_POINT_COMPUTE, *_normalResolveShader, 0, resources);
    commandBuffer.PushConstants(*_normalResolveShader, 0, &pc);

    commandBuffer.Dispatch(
        (_screenExtent.width  + 7) / 8,
        (_screenExtent.height + 7) / 8, 1);

    commandBuffer.TransitionImageLayout(resolvedImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void DFAOPass::UpdateParams()
{
    _params.VolumeResolution = vec4(
        static_cast<float>(SDF_VOLUME_DIM),
        static_cast<float>(SDF_VOLUME_DIM),
        static_cast<float>(SDF_VOLUME_DIM),
        0.0f);
    _params.NumSamples    = _numSamples;
    _params.MaxDistance   = _maxDistance;
    _params.Intensity     = _intensity;
    _params.StepScale     = _stepScale;
    _params.PaddingFactor = 0.1f;
    _params.MinAO         = _minAO;
}

void DFAOPass::UpdateGUI()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Debug"))
        {
            ImGui::MenuItem("DFAO", nullptr, &_showWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (!_showWindow)
        return;

    if (!ImGui::Begin("DFAO Debug", &_showWindow))
    {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Enable DFAO", &_enabled);

    if (_enabled)
    {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderInt  ("Num Samples",  &_numSamples,  1,    16);
            ImGui::SliderFloat("Max Distance", &_maxDistance, 0.1f, 10.0f);
            ImGui::SliderFloat("Intensity",    &_intensity,   0.0f, 2.0f);
            ImGui::SliderFloat("Step Scale",   &_stepScale,   0.1f, 2.0f);
            ImGui::SliderFloat("Min AO",       &_minAO,       0.0f, 1.0f);
        }

        if (_aoTexture && ImGui::CollapsingHeader("AO Map", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (_aoImGuiDS == VK_NULL_HANDLE)
            {
                _aoImGuiDS = ImGui_ImplVulkan_AddTexture(
                    _aoTexture->GetSampler()->GetSampler(),
                    _aoTexture->GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            float    aspect   = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
            uint32_t previewW = static_cast<uint32_t>(128 * aspect);
            ImGui::Image(static_cast<ImTextureID>(_aoImGuiDS),
                ImVec2(static_cast<float>(previewW), 128.0f));
        }
    }

    ImGui::End();
}

void DFAOPass::Prepare()
{
}

void DFAOPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
    // Always update GUI so the checkbox remains accessible
    UpdateGUI();

    if (!_enabled)
        return;

    if (!_sdfGenerator || !_sdfGenerator->IsGenerated())
        return;

    auto  sdfTexture   = _sdfGenerator->GetSDFTexture();
    auto* boundsBuffer = _sdfGenerator->GetBoundsBuffer();
    if (!sdfTexture || !boundsBuffer)
        return;

    auto normalTexture = renderFrame.GetRenderTarget("MainNormal");
    if (!normalTexture)
        return;

    EnsureRenderTargets(renderFrame);
    UpdateParams();

    auto& commandBuffer = renderFrame.GetCommandBuffer();
    commandBuffer.BeginDebugMarker("DFAO");

    // Reuse the resolved depth buffer from SDFShadowPass
    shared_ptr<Texture> depthForSampling;
    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        depthForSampling = renderFrame.GetRenderTarget("SDFResolvedDepth");
        if (!depthForSampling)
            return;
    }
    else
    {
        depthForSampling = renderFrame.GetRenderTarget("MainDepth");
        if (!depthForSampling)
            return;

        commandBuffer.TransitionImageLayout(*depthForSampling->GetImage().lock(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Normal resolve
    shared_ptr<Texture> normalForSampling = normalTexture;
    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        ResolveNormal(renderFrame, commandBuffer, normalTexture);
        normalForSampling = _resolvedNormalTexture;
    }

    auto& aoImage = *_aoTexture->GetImage().lock();
    commandBuffer.TransitionImageLayout(aoImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    PerspectiveCamera* camera = _scene.GetMainCamera();
    glm::mat4 invProj = glm::inverse(camera->Matrices.Projection);
    glm::mat4 invView = glm::inverse(camera->Matrices.View);

    auto builder = renderFrame.CreateDescriptorSetBuilder(*_dfaoShader, 0);
    builder.SetTextureBuffer(0, depthForSampling);
    builder.SetTextureBuffer(1, normalForSampling);
    builder.SetTextureBuffer(2, sdfTexture);
    builder.SetTextureBuffer(3, _aoTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    builder.SetStorageBuffer(4, boundsBuffer);
    builder.SetUniformBuffer(5, &_params);
    auto& resources = builder.Build();

    struct DFAOPushConstants
    {
        glm::mat4 InvView;
        glm::mat4 InvProj;
        glm::vec2 ScreenSize;
        float     _pad[2];
    } pc = {
        invView,
        invProj,
        glm::vec2(static_cast<float>(_screenExtent.width),
                  static_cast<float>(_screenExtent.height)),
        { 0.0f, 0.0f }
    };

    commandBuffer.BindPipeline(_dfaoPipeline.get());
    commandBuffer.BindDescriptorSet(renderFrame,
        VK_PIPELINE_BIND_POINT_COMPUTE, *_dfaoShader, 0, resources);
    commandBuffer.PushConstants(*_dfaoShader, 0, &pc);

    commandBuffer.Dispatch(
        (_screenExtent.width / 2 + 7) / 8,
        (_screenExtent.height / 2 + 7) / 8, 1);

    commandBuffer.TransitionImageLayout(aoImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
    {
        commandBuffer.TransitionImageLayout(*depthForSampling->GetImage().lock(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    commandBuffer.EndDebugMarker();
}