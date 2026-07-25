#include "stdafx.h"
#include "DFAOPass.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/SDFGenerator.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/ResourceCache.h"
#include "AmbientOcclusionPass.h"
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
    _dfaoShader   = _device.GetResourceCache().LoadShader("Shaders/dfao.comp.spv");
    _dfaoPipeline = make_unique<Pipeline>(_device, _dfaoShader.Get());
}

DFAOPass::~DFAOPass()
{
    if (_aoImGuiDS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(_aoImGuiDS);
}

void DFAOPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc aoDesc{};
    aoDesc.extent  = { _screenExtent.width, _screenExtent.height };
    aoDesc.format  = VK_FORMAT_R8_UNORM;
    aoDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    aoDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    aoDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    // GeometryPass (graphics) may sample this before the first compute
    // production, so start it in the layout the consumer expects.
    aoDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    _aoTexture = renderFrame.GetResources().GetOrCreateRenderTarget(AmbientOcclusionPass::RT_AO, aoDesc);
}

void DFAOPass::UpdateParams()
{
    _params.VolumeResolution = vec4(
        static_cast<float>(SDF_VOLUME_DIM),
        static_cast<float>(SDF_VOLUME_DIM),
        static_cast<float>(SDF_VOLUME_DIM),
        0.0f);
    _params.NumSamples = _numSamples;
    _params.MaxDistance = _maxDistance;
    _params.Intensity = _intensity;
    _params.StepScale = _stepScale;
    _params.ContactShadowStrength = _contactShadowStrength;
    _params.ContactThreshold = _contactThreshold;
    _params.PaddingFactor = 0.1f;
}

void DFAOPass::UpdateGUI()
{
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderInt  ("Num Samples",  &_numSamples,  1,    16);
        ImGui::SliderFloat("Max Distance", &_maxDistance, 0.1f, 10.0f);
        ImGui::SliderFloat("Intensity",    &_intensity,   0.0f, 2.0f);
        ImGui::SliderFloat("Step Scale",   &_stepScale,   0.1f, 2.0f);
        ImGui::SliderFloat("Contact Strength", &_contactShadowStrength, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Contact Threshold", &_contactThreshold, 0.01f, 0.5f, "%.3f");
    }

    if (_aoTexture.IsValid() && ImGui::CollapsingHeader("AO Map", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (_aoImGuiDS == VK_NULL_HANDLE)
        {
            _aoImGuiDS = ImGui_ImplVulkan_AddTexture(
                _aoTexture.Get().GetVkSampler(),
                _aoTexture.Get().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        float    aspect   = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
        uint32_t previewW = static_cast<uint32_t>(128 * aspect);
        ImGui::Image(static_cast<ImTextureID>(_aoImGuiDS),
            ImVec2(static_cast<float>(previewW), 128.0f));
    }
}

void DFAOPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
    auto& frameResources = renderFrame.GetResources();
    if (!_sdfGenerator || !_sdfGenerator->IsGenerated())
        return;

    auto  sdfTexture   = _sdfGenerator->GetSDFTexture();
    auto* boundsBuffer = _sdfGenerator->GetBoundsBuffer();
    if (!sdfTexture.IsValid() || !boundsBuffer)
        return;

    UpdateParams();
    commandBuffer.BeginDebugMarker("DFAO");

    auto depthForSampling = frameResources.GetCurrentDepth();
    auto normalForSampling = frameResources.GetCurrentNormal();
    if (!depthForSampling.IsValid() || !normalForSampling.IsValid())
        return;

    commandBuffer.TransitionImageLayout(_aoTexture.Get(),
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    PerspectiveCamera* camera = _scene.GetMainCamera();
    glm::mat4 invProj = glm::inverse(camera->Matrices.Projection);
    glm::mat4 invView = glm::inverse(camera->Matrices.View);

    auto& dfaoShader = _dfaoShader.Get();
    auto builder = frameResources.CreateDescriptorSetBuilder(dfaoShader, 0);
    builder.SetTextureBuffer(0, depthForSampling);
    builder.SetTextureBuffer(1, normalForSampling);
    builder.SetTextureBuffer(2, sdfTexture);
    builder.SetTextureBuffer(3, _aoTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    auto& paramsBuffer = frameResources.GetOrCreateUniformBuffer<DFAOUniform>("DFAOPass.Params").Get();
    paramsBuffer.Update(_params);

    builder.SetStorageBuffer(4, *boundsBuffer);
    builder.SetUniformBuffer(5, paramsBuffer);
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
    commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
        dfaoShader, resources);
    commandBuffer.PushConstants(dfaoShader, 0, pc);

    commandBuffer.Dispatch(
        (_screenExtent.width + 7) / 8,
        (_screenExtent.height + 7) / 8, 1);

    commandBuffer.TransitionImageLayout(_aoTexture.Get(),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    commandBuffer.EndDebugMarker();
}