#pragma once
#include "RenderPass.h"
using namespace Core;

class GUIRenderPass : public Core::RenderPass
{
public:
	GUIRenderPass(Core::Device& device, Core::SwapChain& swapChain);
	virtual ~GUIRenderPass() override;

	virtual void Prepare() override;
	virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;

	void Update();
private:
	void UpdateFrame();
private:
	VkDescriptorPool _pool;
};

