#include "stdafx.h"
#include "CACAOPass.h"
#include "Foundation/Scene.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Components/PerspectiveCamera.h"
#include "DFAOPass.h"
using namespace Core;

CACAOPass::CACAOPass(Device& device, WorkerThreadManager& workerThreadManager,
    Scene& scene, VkExtent2D screenExtent,
    VkSampleCountFlagBits msaaSamples)
    : RendererPass(device, workerThreadManager)
    , _scene(scene)
    , _screenExtent(screenExtent)
	, _msaaSamples(msaaSamples)
{
    // Allocate opaque context on heap using the size reported by the library
    size_t contextSize = FFX_CACAO_VkGetContextSize();
    m_cacaoContext = reinterpret_cast<FFX_CACAO_VkContext*>(malloc(contextSize));

    FFX_CACAO_VkCreateInfo createInfo = {};
    createInfo.physicalDevice = device.GetPhysicalDevice();
    createInfo.device = device.GetDevice();
    createInfo.flags = 0;

    FFX_CACAO_Status status = FFX_CACAO_VkInitContext(m_cacaoContext, &createInfo);
    if (status != FFX_CACAO_STATUS_OK)
    {
        free(m_cacaoContext);
        m_cacaoContext = nullptr;
        throw std::runtime_error("Failed to initialize FFX CACAO Vulkan context");
    }

    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        _normalResolveShader = _device.GetResourceCache().RequestShader("Shaders/normalResolve.comp.spv");
        _normalResolvePipeline = make_unique<Pipeline>(_device, *_normalResolveShader);
    }
}

CACAOPass::~CACAOPass()
{
    if (m_cacaoContext)
    {
        FFX_CACAO_VkDestroyScreenSizeDependentResources(m_cacaoContext);
        FFX_CACAO_VkDestroyContext(m_cacaoContext);
        free(m_cacaoContext);
        m_cacaoContext = nullptr;
    }

    if (_aoImGuiDS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(_aoImGuiDS);
}

void CACAOPass::Prepare()
{
    
}

void CACAOPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
    UpdateGUI();

    if (!_enabled)
        return;

    EnsureRenderTargets(renderFrame);

    CommandBuffer& commandBuffer = renderFrame.GetCommandBuffer();

    // Get depth and normal render targets from previous passes
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

    auto normalTexture = renderFrame.GetRenderTarget("MainNormal");

    if (!depthForSampling || !normalTexture)
    {
        // Required inputs not available
        return;
    }

    // Normal resolve
    shared_ptr<Texture> normalForSampling = normalTexture;
    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        ResolveNormal(renderFrame, commandBuffer, normalTexture);
        normalForSampling = _resolvedNormalTexture;
    }

    // Get image views from textures
    VkImageView depthView = depthForSampling->GetImageView();
    VkImageView normalView = normalForSampling->GetImageView();
    VkImageView outputView = _aoTexture->GetImageView();

    // Bind screen-size-dependent resources
    FFX_CACAO_VkScreenSizeInfo sizeInfo = {};
    sizeInfo.width = _screenExtent.width;
    sizeInfo.height = _screenExtent.height;
    sizeInfo.depthView = depthForSampling->GetImageView();
    sizeInfo.normalsView = normalForSampling->GetImageView();
    sizeInfo.output = _aoTexture->GetImage().lock().get()->GetImage();
    sizeInfo.outputView = _aoTexture->GetImageView();
    sizeInfo.useDownsampledSsao = FFX_CACAO_FALSE;

    FFX_CACAO_VkInitScreenSizeDependentResources(m_cacaoContext, &sizeInfo);

    // Apply current settings
    FFX_CACAO_Settings cacaoSettings = FFX_CACAO_DEFAULT_SETTINGS;
    cacaoSettings.radius = m_settings.Radius;
    cacaoSettings.shadowMultiplier = m_settings.ShadowMultiplier;
    cacaoSettings.shadowPower = m_settings.ShadowPower;
    cacaoSettings.shadowClamp = m_settings.ShadowClamp;
    cacaoSettings.horizonAngleThreshold = m_settings.HorizonAngleThreshold;
    cacaoSettings.fadeOutFrom = m_settings.FadeOutFrom;
    cacaoSettings.fadeOutTo = m_settings.FadeOutTo;
    cacaoSettings.qualityLevel = static_cast<FFX_CACAO_Quality>(m_settings.QualityLevel);
    cacaoSettings.adaptiveQualityLimit = m_settings.AdaptiveQualityLimit;
    cacaoSettings.blurPassCount = m_settings.BlurPassCount;
    cacaoSettings.sharpness = m_settings.Sharpness;
    cacaoSettings.detailShadowStrength = m_settings.DetailShadowStrength;
    cacaoSettings.generateNormals = m_settings.GenerateNormals ? FFX_CACAO_TRUE : FFX_CACAO_FALSE;
    cacaoSettings.bilateralSigmaSquared = m_settings.BilateralSigmaSquared;
    cacaoSettings.bilateralSimilarityDistanceSigma = m_settings.BilateralSimilarityDistanceSigma;

    FFX_CACAO_VkUpdateSettings(m_cacaoContext, &cacaoSettings);

    PerspectiveCamera* camera = _scene.GetMainCamera();
    const mat4& projMatrix = camera->Matrices.Projection;
    mat4 normalToViewMatrix = mat4(1.0f); // Identity for world-space normals

    FFX_CACAO_Matrix4x4 proj, normalsToView;
    memcpy(proj.elements, glm::value_ptr(projMatrix), sizeof(float) * 16);
    memcpy(normalsToView.elements, glm::value_ptr(normalToViewMatrix), sizeof(float) * 16);

    // Dispatch CACAO - internally manages all compute shaders and resources
    FFX_CACAO_VkDraw(m_cacaoContext, commandBuffer.GetHandle(), &proj, &normalsToView);
}

void CACAOPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc aoDesc{};
    aoDesc.extent = { _screenExtent.width / 2, _screenExtent.height / 2 };
    aoDesc.format = VK_FORMAT_R8_UNORM;
    aoDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    aoDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    aoDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    _aoTexture = renderFrame.GetOrCreateRenderTarget(DFAOPass::RT_DFAO, aoDesc);

    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        RenderTargetDesc normalDesc{};
        normalDesc.extent = _screenExtent;
        normalDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        normalDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        normalDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        normalDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        _resolvedNormalTexture = renderFrame.GetOrCreateRenderTarget(DFAOPass::RT_NORMAL_RESOLVED, normalDesc);
    }
}

void CACAOPass::ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
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
        (_screenExtent.width + 7) / 8,
        (_screenExtent.height + 7) / 8, 1);

    commandBuffer.TransitionImageLayout(resolvedImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void CACAOPass::UpdateGUI()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Debug"))
        {
            ImGui::MenuItem("CACAO", nullptr, &_showWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (!_showWindow)
        return;

    if (ImGui::Begin("CACAO Settings", &_showWindow))
    {
		ImGui::End();
        return;
    }

    ImGui::Checkbox("Enable CACAO", &_enabled);

    ImGui::Text("AMD Combined Adaptive Compute Ambient Occlusion");
    ImGui::Separator();

    bool paramsChanged = false;

    ImGui::Text("Basic Settings");
    if (ImGui::SliderFloat("Radius", &m_settings.Radius, 0.1f, 5.0f, "%.2f"))
        paramsChanged = true;

    if (ImGui::SliderFloat("Shadow Multiplier", &m_settings.ShadowMultiplier, 0.0f, 5.0f, "%.2f"))
        paramsChanged = true;

    if (ImGui::SliderFloat("Shadow Power", &m_settings.ShadowPower, 0.5f, 5.0f, "%.2f"))
        paramsChanged = true;

    if (ImGui::SliderFloat("Shadow Clamp", &m_settings.ShadowClamp, 0.0f, 1.0f, "%.3f"))
        paramsChanged = true;

    ImGui::Spacing();
    ImGui::Text("Quality Settings");
    ImGui::Separator();

    const char* qualityLevels[] = { "Low", "Medium", "High", "Highest" };
    if (ImGui::Combo("Quality Level", &m_settings.QualityLevel, qualityLevels, 4))
        paramsChanged = true;

    if (ImGui::SliderFloat("Adaptive Quality Limit", &m_settings.AdaptiveQualityLimit, 0.0f, 1.0f, "%.3f"))
        paramsChanged = true;

    ImGui::Spacing();
    ImGui::Text("Blur Settings");
    ImGui::Separator();

    if (ImGui::SliderInt("Blur Pass Count", &m_settings.BlurPassCount, 0, 8))
        paramsChanged = true;

    if (ImGui::SliderFloat("Sharpness", &m_settings.Sharpness, 0.0f, 1.0f, "%.3f"))
        paramsChanged = true;

    ImGui::Spacing();
    ImGui::Text("Advanced Settings");
    ImGui::Separator();

    if (ImGui::SliderFloat("Horizon Angle Threshold", &m_settings.HorizonAngleThreshold, 0.0f, 0.2f, "%.3f"))
        paramsChanged = true;

    if (ImGui::SliderFloat("Fade Out From", &m_settings.FadeOutFrom, 0.0f, 100.0f, "%.1f"))
        paramsChanged = true;

    if (ImGui::SliderFloat("Fade Out To", &m_settings.FadeOutTo, 0.0f, 500.0f, "%.1f"))
        paramsChanged = true;

    if (ImGui::SliderFloat("Detail Shadow Strength", &m_settings.DetailShadowStrength, 0.0f, 2.0f, "%.2f"))
        paramsChanged = true;

    if (ImGui::Checkbox("Generate Normals", &m_settings.GenerateNormals))
        paramsChanged = true;

    ImGui::Spacing();

    if (ImGui::Button("Reset to Defaults"))
    {
        m_settings = Settings();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Display AO result texture
    if (_aoImGuiDS != VK_NULL_HANDLE)
    {
        ImGui::Text("AO Output Preview:");
        ImVec2 previewSize(256, 256);
        ImGui::Image(_aoImGuiDS, previewSize);
    }

    ImGui::End();
}