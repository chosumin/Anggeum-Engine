#include "stdafx.h"
#include "AmbientOcclusionPass.h"

namespace Core
{
    AmbientOcclusionPass::AmbientOcclusionPass(Device& device,
        WorkerThreadManager& workerThreadManager,
        Scene& scene, VkExtent2D screenExtent,
        VkSampleCountFlagBits msaaSamples,
        SDFGenerator* sdfGenerator)
        : RendererPass(device, workerThreadManager)
    {
        _cacaoPass = make_unique<CACAOPass>(device, workerThreadManager,
            scene, screenExtent, msaaSamples);

        _dfaoPass = make_unique<DFAOPass>(device, workerThreadManager,
            scene, screenExtent, msaaSamples, sdfGenerator);

        // Initial state: CACAO enabled, DFAO disabled
        SetMethod(AOMethod::CACAO);
    }

    AmbientOcclusionPass::~AmbientOcclusionPass()
    {
    }

    void AmbientOcclusionPass::EnsureRenderTargets(RenderFrame& renderFrame)
    {
        switch (_activeMethod)
        {
        case AOMethod::CACAO:
            _cacaoPass->EnsureRenderTargets(renderFrame);
            break;
        case AOMethod::DFAO:
            _dfaoPass->EnsureRenderTargets(renderFrame);
            break;
        }
    }

    void AmbientOcclusionPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        switch (_activeMethod)
        {
        case AOMethod::CACAO:
            _cacaoPass->Draw(renderFrame, commandBuffer, imageIndex);
            break;
        case AOMethod::DFAO:
            _dfaoPass->Draw(renderFrame, commandBuffer, imageIndex);
            break;
        }

        // Signal compute timeline so graphics queue (GeometryPass) can consume AO output
        renderFrame.GetCurrentSubmitInfo().AddSignalSemaphore(QueueType::Compute);
    }

    void AmbientOcclusionPass::SetMethod(AOMethod method)
    {
        _activeMethod = method;
    }

    void AmbientOcclusionPass::OnGUI(RenderFrame& renderFrame)
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
}