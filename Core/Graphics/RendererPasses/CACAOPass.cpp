#include "stdafx.h"
#include "CACAOPass.h"
#include "Foundation/Scene.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/FrameResources.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/FrameGraph/Passes/FGAmbientOcclusionPass.h"

#include "ffx_cacao_impl.h"

using namespace Core;

CACAOPass::CACAOPass(Device& device, Scene& scene, VkExtent2D screenExtent,
    VkSampleCountFlagBits msaaSamples)
    : _device(device)
    , _scene(scene)
    , _screenExtent(screenExtent)
    , _msaaSamples(msaaSamples)
{
    // CACAO contexts are created lazily in GetOrCreateCacaoContext(),
    // one per frame-in-flight slot, so we don't allocate anything here.
}

CACAOPass::~CACAOPass()
{
    // Destroy every per-frame CACAO context
    for (auto& [index, ctx] : m_cacaoContexts)
    {
        if (ctx)
        {
            FFX_CACAO_VkDestroyScreenSizeDependentResources(ctx);
            FFX_CACAO_VkDestroyContext(ctx);
            free(ctx);
        }
    }
    m_cacaoContexts.clear();

    if (_aoImGuiDS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(_aoImGuiDS);
}

FFX_CACAO_VkContext* CACAOPass::GetOrCreateCacaoContext(
    FrameResources* frameKey,
    VkImageView depthView,
    VkImageView normalsView,
    VkImage outputImage,
    VkImageView outputView)
{
    auto it = m_cacaoContexts.find(frameKey);
    if (it != m_cacaoContexts.end())
        return it->second;

    // Allocate a fresh context for this frame-in-flight slot
    size_t contextSize = FFX_CACAO_VkGetContextSize();
    FFX_CACAO_VkContext* ctx = static_cast<FFX_CACAO_VkContext*>(malloc(contextSize));
    if (!ctx)
        throw std::runtime_error("Failed to allocate CACAO context");

    FFX_CACAO_VkCreateInfo createInfo = {};
    createInfo.physicalDevice = _device.GetPhysicalDevice();
    createInfo.device         = _device.GetDevice();
    createInfo.flags          = 0;

    FFX_CACAO_Status status = FFX_CACAO_VkInitContext(ctx, &createInfo);
    if (status != FFX_CACAO_STATUS_OK)
    {
        free(ctx);
        throw std::runtime_error("Failed to initialize CACAO context");
    }

    // Bind screen-size-dependent resources for this frame slot's render targets
    FFX_CACAO_VkScreenSizeInfo sizeInfo = {};
    sizeInfo.width              = _screenExtent.width;
    sizeInfo.height             = _screenExtent.height;
    sizeInfo.depthView          = depthView;
    sizeInfo.normalsView        = normalsView;
    sizeInfo.output             = outputImage;
    sizeInfo.outputView         = outputView;
    sizeInfo.useDownsampledSsao = FFX_CACAO_FALSE;

    FFX_CACAO_VkInitScreenSizeDependentResources(ctx, &sizeInfo);

    m_cacaoContexts[frameKey] = ctx;
    return ctx;
}

bool CACAOPass::Prepare(FrameResources& frameResources, Handle<Texture> depth, Handle<Texture> normal)
{
    _currentContext = nullptr;

    if (!depth.IsValid() || !normal.IsValid())
        return false;

    PerspectiveCamera* camera = _scene.GetMainCamera();
    if (!camera)
        return false;

    auto& aoImage = _aoTexture.Get().GetImage();

    FFX_CACAO_VkContext* ctx = GetOrCreateCacaoContext(
        &frameResources,
        depth.Get().GetImageView(),
        normal.Get().GetImageView(),
        aoImage.GetImage(),
        _aoTexture.Get().GetImageView());

    // Apply current settings
    FFX_CACAO_Settings cacaoSettings = {};
    cacaoSettings.radius                           = m_settings.Radius;
    cacaoSettings.shadowMultiplier                 = m_settings.ShadowMultiplier;
    cacaoSettings.shadowPower                      = m_settings.ShadowPower;
    cacaoSettings.shadowClamp                      = m_settings.ShadowClamp;
    cacaoSettings.horizonAngleThreshold            = m_settings.HorizonAngleThreshold;
    cacaoSettings.fadeOutFrom                      = m_settings.FadeOutFrom;
    cacaoSettings.fadeOutTo                        = m_settings.FadeOutTo;
    cacaoSettings.qualityLevel                     = static_cast<FFX_CACAO_Quality>(m_settings.QualityLevel);
    cacaoSettings.adaptiveQualityLimit             = m_settings.AdaptiveQualityLimit;
    cacaoSettings.blurPassCount                    = m_settings.BlurPassCount;
    cacaoSettings.sharpness                        = m_settings.Sharpness;
    cacaoSettings.detailShadowStrength             = m_settings.DetailShadowStrength;
    cacaoSettings.generateNormals                  = m_settings.GenerateNormals ? FFX_CACAO_TRUE : FFX_CACAO_FALSE;
    cacaoSettings.bilateralSigmaSquared            = m_settings.BilateralSigmaSquared;
    cacaoSettings.bilateralSimilarityDistanceSigma = m_settings.BilateralSimilarityDistanceSigma;

    FFX_CACAO_VkUpdateSettings(ctx, &cacaoSettings);

    mat4 projMatrix = camera->Matrices.Projection;
    projMatrix[1][1] = -projMatrix[1][1];

    // CACAO expects a world-space normal -> view-space normal matrix.
    // The reference sample feeds: zFlip * inverse(view), where zFlip negates
    // the Z axis to convert from the engine's right-handed view space into the
    // left-handed view space CACAO operates in. Because GLM is column-major and
    // the FFX matrix is consumed row-major, copying inverse(view) * zFlip here
    // produces exactly the same data the DX/VK sample passes to CACAO.
    const mat4& viewMatrix = camera->Matrices.View;
    mat4 zFlip = mat4(1.0f);
    zFlip[2][2] = -1.0f;
    mat4 normalsWorldToView = glm::inverse(viewMatrix) * zFlip;

    memcpy(_proj.elements, glm::value_ptr(projMatrix), sizeof(float) * 16);
    memcpy(_normalsToView.elements, glm::value_ptr(normalsWorldToView), sizeof(float) * 16);

    _currentContext = ctx;
    return true;
}

void CACAOPass::Record(CommandBuffer& commandBuffer)
{
    assert(_currentContext != nullptr && "Record without a successful Prepare");
    FFX_CACAO_VkDraw(_currentContext, commandBuffer.GetHandle(), &_proj, &_normalsToView);
}

void CACAOPass::EnsureRenderTargets(FrameResources& frameResources)
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
    _aoTexture = frameResources.GetOrCreateRenderTarget(FGAmbientOcclusionPass::RT_AO, aoDesc);
}

void CACAOPass::UpdateGUI()
{
    ImGui::Text("AMD Combined Adaptive Compute Ambient Occlusion");
    ImGui::Separator();

    ImGui::Text("Basic Settings");
    ImGui::SliderFloat("Radius", &m_settings.Radius, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Shadow Multiplier", &m_settings.ShadowMultiplier, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Shadow Power", &m_settings.ShadowPower, 0.5f, 5.0f, "%.2f");
    ImGui::SliderFloat("Shadow Clamp", &m_settings.ShadowClamp, 0.0f, 1.0f, "%.3f");

    ImGui::Spacing();
    ImGui::Text("Quality Settings");
    ImGui::Separator();

    const char* qualityLevels[] = { "Lowest", "Low", "Medium", "High", "Highest" };
    ImGui::Combo("Quality Level", &m_settings.QualityLevel, qualityLevels, 5);
    ImGui::SliderFloat("Adaptive Quality Limit", &m_settings.AdaptiveQualityLimit, 0.0f, 1.0f, "%.3f");

    ImGui::Spacing();
    ImGui::Text("Blur Settings");
    ImGui::Separator();

    ImGui::SliderInt("Blur Pass Count", &m_settings.BlurPassCount, 0, 8);
    ImGui::SliderFloat("Sharpness", &m_settings.Sharpness, 0.0f, 1.0f, "%.3f");

    ImGui::Spacing();
    ImGui::Text("Advanced Settings");
    ImGui::Separator();

    ImGui::SliderFloat("Horizon Angle Threshold", &m_settings.HorizonAngleThreshold, 0.0f, 0.2f, "%.3f");
    ImGui::SliderFloat("Fade Out From", &m_settings.FadeOutFrom, 0.0f, 100.0f, "%.1f");
    ImGui::SliderFloat("Fade Out To", &m_settings.FadeOutTo, 0.0f, 500.0f, "%.1f");
    ImGui::SliderFloat("Detail Shadow Strength", &m_settings.DetailShadowStrength, 0.0f, 2.0f, "%.2f");
    ImGui::Checkbox("Generate Normals", &m_settings.GenerateNormals);

    ImGui::Spacing();

    if (ImGui::Button("Reset to Defaults"))
    {
        m_settings = Settings();
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (_aoImGuiDS != VK_NULL_HANDLE)
    {
        ImGui::Text("AO Output Preview:");
        ImVec2 previewSize(256, 256);
        ImGui::Image(_aoImGuiDS, previewSize);
    }
}
