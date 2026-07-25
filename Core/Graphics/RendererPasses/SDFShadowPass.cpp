#include "stdafx.h"
#include "SDFShadowPass.h"
#include "Foundation/Scene.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/SDFGenerator.h"
#include "Graphics/MeshBufferManager.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/ResourceCache.h"
#include "ShadowPass.h"
using namespace Core;

static constexpr uint32_t DEBUG_SLICE_HEIGHT = 256;

SDFShadowPass::SDFShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
    Scene& scene, VkExtent2D screenExtent,
    VkSampleCountFlagBits msaaSamples, ShadowPass& shadowPass)
    : RendererPass(device, workerThreadManager)
    , _scene(scene)
    , _screenExtent(screenExtent)
    , _msaaSamples(msaaSamples)
    , _shadowPass(shadowPass)
{
    _sdfGenerator = make_unique<SDFGenerator>(device);

    _sdfShadowShader = _device.GetResourceCache().LoadShader("Shaders/sdfShadow.comp.spv");
    _sdfShadowPipeline = make_unique<Pipeline>(_device, _sdfShadowShader.Get());

    _volumeSliceShader = _device.GetResourceCache().LoadShader("Shaders/sdfVolumeSlice.comp.spv");
    _volumeSlicePipeline = make_unique<Pipeline>(_device, _volumeSliceShader.Get());
}

SDFShadowPass::~SDFShadowPass()
{
    if (_sdfShadowImGuiDS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(_sdfShadowImGuiDS);
    if (_volumeSliceImGuiDS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(_volumeSliceImGuiDS);
}

void SDFShadowPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc sdfShadowDesc{};
    sdfShadowDesc.extent = {
        _screenExtent.width / 2,
        _screenExtent.height / 2
    };
    sdfShadowDesc.format  = VK_FORMAT_R8_UNORM;
    sdfShadowDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    sdfShadowDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    sdfShadowDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    // GeometryPass (graphics) may sample this before the first compute
    // production, so start it in the layout the consumer expects.
    sdfShadowDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    _sdfShadowTexture = renderFrame.GetOrCreateRenderTarget(RT_SDF_SHADOW, sdfShadowDesc);

    // Volume raytrace debug texture - match screen aspect ratio
    float aspect = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
    uint32_t sliceWidth = static_cast<uint32_t>(DEBUG_SLICE_HEIGHT * aspect);

    RenderTargetDesc sliceDesc{};
    sliceDesc.extent  = { sliceWidth, DEBUG_SLICE_HEIGHT };
    sliceDesc.format  = VK_FORMAT_R8G8B8A8_UNORM;
    sliceDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    sliceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    sliceDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;

    _volumeSliceTexture = renderFrame.GetOrCreateRenderTarget(RT_SDF_VOLUME_SLICE, sliceDesc);
}

void SDFShadowPass::RenderVolumeSlice(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
    auto sdfTexture = _sdfGenerator->GetSDFTexture();
    auto* boundsBuffer = _sdfGenerator->GetBoundsBuffer();
    if (!sdfTexture.IsValid() || !_volumeSliceTexture.IsValid() || !boundsBuffer)
        return;

    commandBuffer.TransitionImageLayout(_volumeSliceTexture.Get(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    PerspectiveCamera* camera = _scene.GetMainCamera();
    glm::mat4 viewMatrix = camera->Matrices.View;
    glm::vec3 camPos = camera->Matrices.Position;

    glm::vec3 forward = -glm::vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);
    glm::vec3 right   = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
    glm::vec3 up      = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);

    struct VolumeRaytracePushConstants {
        glm::vec4 cameraPos;
        glm::vec4 cameraForward;
        glm::vec4 cameraRight;
        glm::vec4 cameraUp;
        float fov;
        float maxDistance;
        int32_t maxSteps;
        float hitThreshold;
        float paddingFactor;
    } pc = {
        glm::vec4(camPos, 0.0f),
        glm::vec4(forward, 0.0f),
        glm::vec4(right, 0.0f),
        glm::vec4(up, 0.0f),
        camera->GetFieldOfView(),
        200.0f,
        _debugMaxSteps,
        _debugHitThreshold,
        0.1f
    };

    auto& volumeSliceShader = _volumeSliceShader.Get();
    auto sliceBuilder = renderFrame.CreateDescriptorSetBuilder(volumeSliceShader, 0);
    sliceBuilder.SetTextureBuffer(0, sdfTexture);
    sliceBuilder.SetTextureBuffer(1, _volumeSliceTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    sliceBuilder.SetStorageBuffer(2, *boundsBuffer);
    auto& sliceResources = sliceBuilder.Build();

    commandBuffer.BindPipeline(_volumeSlicePipeline.get());
    commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, volumeSliceShader,
        sliceResources);
    commandBuffer.PushConstants(volumeSliceShader, 0, pc);

    float    aspect     = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
    uint32_t sliceWidth = static_cast<uint32_t>(DEBUG_SLICE_HEIGHT * aspect);

    commandBuffer.Dispatch((sliceWidth + 7) / 8, (DEBUG_SLICE_HEIGHT + 7) / 8, 1);

    commandBuffer.TransitionImageLayout(_volumeSliceTexture.Get(),
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SDFShadowPass::UpdateSDFParams()
{
    auto lights = _scene.GetComponents<Light>();
    if (!lights.empty())
    {
        auto light = lights[0];
        auto& transform = light->GetEntity().GetTransform();
        glm::mat4 lightMatrix = transform.GetMatrix();
        glm::vec3 lightDir = glm::normalize(glm::vec3(lightMatrix[2]));
        _sdfParams.LightDirection = vec4(lightDir, _shadowSoftness);
    }

    _sdfParams.VolumeResolution = vec4(
        static_cast<float>(SDF_VOLUME_DIM),
        static_cast<float>(SDF_VOLUME_DIM),
        static_cast<float>(SDF_VOLUME_DIM),
        _maxDistance);
    _sdfParams.MaxSteps       = _maxSteps;
    _sdfParams.MinDistance    = _minDistance;
    _sdfParams.MaxDistance    = _maxDistance;
    _sdfParams.ShadowSoftness = _shadowSoftness;
    _sdfParams.PaddingFactor  = 0.1f;

    // Copy transition parameters from ShadowPass
    auto* shadowUniform = _shadowPass.GetShadowBuffer();
    _sdfParams.SDFTransitionDistance = shadowUniform->SDFTransitionDistance;
    _sdfParams.SDFTransitionRange    = shadowUniform->SDFTransitionRange;
}

void SDFShadowPass::OnGUI(RenderFrame& renderFrame)
{
    if (!ImGui::CollapsingHeader("SDF Shadow"))
        return;

    if (ImGui::Button("Generate SDF Texture"))
        _regenerateRequested = true;

    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Shadow Softness", &_shadowSoftness, 1.0f, 32.0f);
        ImGui::SliderFloat("Min Distance",    &_minDistance,    0.0001f, 0.1f, "%.4f");
        ImGui::SliderFloat("Max Distance",    &_maxDistance,    10.0f, 500.0f);
        ImGui::SliderInt  ("Max Steps",       &_maxSteps,       8, 128);

        ImGui::TextDisabled("Cache: %s", _sdfGenerator->GetCachePath().c_str());
    }

    float aspect     = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
    float previewWidth = DEBUG_SLICE_HEIGHT * aspect;

    if (_sdfShadowTexture.IsValid() && ImGui::CollapsingHeader("Shadow Map", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (_sdfShadowImGuiDS == VK_NULL_HANDLE)
        {
            _sdfShadowImGuiDS = ImGui_ImplVulkan_AddTexture(
                _sdfShadowTexture.Get().GetVkSampler(),
                _sdfShadowTexture.Get().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        ImGui::Image(static_cast<ImTextureID>(_sdfShadowImGuiDS),
            ImVec2(previewWidth, DEBUG_SLICE_HEIGHT));
    }

    if (_volumeSliceTexture.IsValid() && ImGui::CollapsingHeader("Volume Raytrace", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (_volumeSliceImGuiDS == VK_NULL_HANDLE)
        {
            _volumeSliceImGuiDS = ImGui_ImplVulkan_AddTexture(
                _volumeSliceTexture.Get().GetVkSampler(),
                _volumeSliceTexture.Get().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        ImGui::SliderFloat("Hit Threshold", &_debugHitThreshold, 0.001f, 0.1f, "%.4f");
        ImGui::SliderInt  ("Ray Max Steps", &_debugMaxSteps,      32, 256);
        ImGui::Image(static_cast<ImTextureID>(_volumeSliceImGuiDS),
            ImVec2(previewWidth, DEBUG_SLICE_HEIGHT));
    }

    ImGui::Separator();
}

void SDFShadowPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
    auto* meshBufferManager = renderFrame.GetMeshBufferManager();
    if (!meshBufferManager)
        return;

    // Deferred save: the SDF was generated on a previous frame
    // is now complete, so it's safe to read back the image/bounds and write to disk.
    if (_savePending)
    {
        _savePending = false;
        _sdfGenerator->SaveToFile(SDF_VOLUME_DIM);
    }

    if (_regenerateRequested || !_sdfGenerator->IsGenerated())
    {
        bool tryLoad = !_regenerateRequested;
        _regenerateRequested = false;

        if (!tryLoad || !_sdfGenerator->TryLoadFromFile(SDF_VOLUME_DIM))
        {
            commandBuffer.BeginDebugMarker("SDF Volume Generation (GPU)");
            _sdfGenerator->Generate(renderFrame, commandBuffer,
                *meshBufferManager,
                SDF_VOLUME_DIM);
            commandBuffer.EndDebugMarker();

            _savePending = true;
        }
    }

    auto sdfTexture = _sdfGenerator->GetSDFTexture();
    if (!sdfTexture.IsValid())
        return;

    UpdateSDFParams();

    auto depthTarget = renderFrame.GetCurrentDepth();
    if (!depthTarget.IsValid())
        return;

    commandBuffer.TransitionImageLayout(_sdfShadowTexture.Get(),
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    PerspectiveCamera* camera = _scene.GetMainCamera();

    auto& cameraBuffer = renderFrame.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA);

    auto& sdfParamsBuffer =
        renderFrame.GetOrCreateUniformBuffer<SDFShadowUniform>("SDFShadowPass.Params");
    sdfParamsBuffer.Update(_sdfParams);

    auto& sdfShadowShader = _sdfShadowShader.Get();
    auto builder = renderFrame.CreateDescriptorSetBuilder(sdfShadowShader, 0);

    builder.SetUniformBuffer(0, cameraBuffer);
    builder.SetTextureBuffer(1, sdfTexture);
    builder.SetUniformBuffer(2, sdfParamsBuffer);
    builder.SetTextureBuffer(3, depthTarget);
    builder.SetTextureBuffer(4, _sdfShadowTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    builder.SetStorageBuffer(5, *_sdfGenerator->GetBoundsBuffer());
    auto& resources = builder.Build();

    commandBuffer.BindPipeline(_sdfShadowPipeline.get());
    commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
        sdfShadowShader,
        resources);

    uint32_t dispatchX = (_screenExtent.width / 2 + 7) / 8;
    uint32_t dispatchY = (_screenExtent.height / 2 + 7) / 8;
    commandBuffer.Dispatch(dispatchX, dispatchY, 1);

    commandBuffer.TransitionImageLayout(_sdfShadowTexture.Get(),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    {
        commandBuffer.BeginDebugMarker("SDF Volume Raytrace Debug");
        RenderVolumeSlice(renderFrame, commandBuffer);
        commandBuffer.EndDebugMarker();
    }
}