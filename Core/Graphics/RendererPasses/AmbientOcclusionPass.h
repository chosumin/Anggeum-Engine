#pragma once
#include "Graphics/RendererPass.h"
#include "CACAOPass.h"
#include "DFAOPass.h"

namespace Core
{
    enum class AOMethod
    {
        CACAO,
        DFAO
    };

    class AmbientOcclusionPass : public RendererPass
    {
    public:
        AmbientOcclusionPass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples,
            SDFGenerator* sdfGenerator);
        ~AmbientOcclusionPass();

        void Prepare() override;
        void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

        void SetMethod(AOMethod method);
        AOMethod GetMethod() const { return _activeMethod; }

        CACAOPass* GetCACAOPass() const { return _cacaoPass.get(); }
        DFAOPass* GetDFAOPass() const { return _dfaoPass.get(); }

    private:
        void UpdateGUI();

    private:
        unique_ptr<CACAOPass> _cacaoPass;
        unique_ptr<DFAOPass> _dfaoPass;
        AOMethod _activeMethod = AOMethod::CACAO;
        bool _showWindow = false;
    };
}