#pragma once
#include "RenderPass.h"
#include "BufferObjects/BufferObjects.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class RendererBatch;
	class InstanceBuffer;
	class Material;
	class ShadowRenderPass : public RenderPass
	{
	public:
		ShadowRenderPass(Device& device,
			Scene& scene, SwapChain& swapChain, RenderTarget* depthRenderTarget);
		virtual ~ShadowRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		void UpdateGUI();
	private:
		Scene& _scene;

		VPBufferObject _directionalLight;
		unordered_map<type_index, RendererBatch*> _batches;
		InstanceBuffer* _instanceBuffer;
		Material* _material;
	};
}

