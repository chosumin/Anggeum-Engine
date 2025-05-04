#pragma once
#include "Core/Graphics/RendererPass.h"

class GUIRenderPass : public Core::RendererPass
{
public:
	GUIRenderPass(Core::Device& device, Core::WorkerThreadManager& workerThreadManager, Core::SwapChain& swapChain, Core::Texture* colorRenderTarget);
	virtual ~GUIRenderPass() override;

	virtual void Prepare() override;
	virtual void Draw(Core::CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;

	void Update();
private:
	void UpdateFrame();
private:
	VkDescriptorPool _pool;
};

