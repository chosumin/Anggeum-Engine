#pragma once
#include "Core/Graphics/RendererPass.h"

class GUIRenderPass : public Core::RendererPass
{
public:
	GUIRenderPass(Core::Device& device, Core::WorkerThreadManager& workerThreadManager, Core::SwapChain& swapChain, Core::Texture* colorRenderTarget);
	virtual ~GUIRenderPass() override;

	virtual void Prepare() override;
	virtual void Draw(Core::RenderFrame& renderFrame, uint32_t frameIndex, uint32_t imageIndex) override;

	void Update();
private:
	void UpdateFrame();
private:
	VkDescriptorPool _pool;
};

