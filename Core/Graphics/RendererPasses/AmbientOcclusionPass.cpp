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

    void AmbientOcclusionPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
    {
        UpdateGUI();

        switch (_activeMethod)
        {
        case AOMethod::CACAO:
            _cacaoPass->Draw(renderFrame, imageIndex);
            break;
        case AOMethod::DFAO:
            _dfaoPass->Draw(renderFrame, imageIndex);
            break;
        }
    }

    void AmbientOcclusionPass::SetMethod(AOMethod method)
    {
        _activeMethod = method;
    }

    void AmbientOcclusionPass::UpdateGUI()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Debug"))
            {
                ImGui::MenuItem("Ambient Occlusion", nullptr, &_showWindow);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (!_showWindow)
            return;

        if (!ImGui::Begin("Ambient Occlusion", &_showWindow))
        {
            ImGui::End();
            return;
        }

        const char* methods[] = { "CACAO (FFX)", "DFAO (Distance Field)" };
        int current = static_cast<int>(_activeMethod);

        if (ImGui::Combo("AO Method", &current, methods, IM_ARRAYSIZE(methods)))
        {
            SetMethod(static_cast<AOMethod>(current));
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Draw active pass settings within this window
        switch (_activeMethod)
        {
        case AOMethod::CACAO:
            _cacaoPass->UpdateGUI();
            break;
        case AOMethod::DFAO:
            _dfaoPass->UpdateGUI();
            break;
        }

        ImGui::End();
    }
}