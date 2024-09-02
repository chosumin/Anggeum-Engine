#pragma once
#include "RenderPass.h"
#include "BufferObjects/BufferObjects.h"

class InstanceData;

namespace Core
{
	class Scene;
	class SwapChain;
	class RendererBatch;
	class InstanceBuffer;

	class GeometryRenderPass : public RenderPass
	{
	public:
		GeometryRenderPass(Device& device, 
			Scene& scene, SwapChain& swapChain, RenderTarget* colorRenderTarget, RenderTarget* depthRenderTarget, RenderTarget* shadowRenderTarget);
		virtual ~GeometryRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;

		void SetShadowBuffer(ShadowUniform& shadowBuffer)
		{
			_shadowBuffer = &shadowBuffer;
		}
	private:
		Scene& _scene;

		unordered_map<type_index, RendererBatch*> _batches;
		InstanceBuffer* _instanceBuffer;

		RenderTarget* _shadowRenderTarget;

		ShadowUniform* _shadowBuffer;
	};
}

