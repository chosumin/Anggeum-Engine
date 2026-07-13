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
        static constexpr const char* RT_AO = "AOResult";

        AmbientOcclusionPass(Device& device, WorkerThreadManager& workerThreadManager,
            Scene& scene, VkExtent2D screenExtent,
            VkSampleCountFlagBits msaaSamples,
            SDFGenerator* sdfGenerator);
        ~AmbientOcclusionPass();

        void EnsureRenderTargets(RenderFrame& renderFrame) override;
        void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex) override;
        void OnGUI(RenderFrame& renderFrame) override;

        QueueType GetQueueType() const override { return QueueType::Compute; }

        void SetMethod(AOMethod method);
        AOMethod GetMethod() const { return _activeMethod; }

        CACAOPass* GetCACAOPass() const { return _cacaoPass.get(); }
        DFAOPass* GetDFAOPass() const { return _dfaoPass.get(); }

    private:
        unique_ptr<CACAOPass> _cacaoPass;
        unique_ptr<DFAOPass> _dfaoPass;
        AOMethod _activeMethod = AOMethod::DFAO;
    };
}
