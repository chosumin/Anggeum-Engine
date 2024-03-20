#pragma once
#include "RenderPass.h"

namespace Core
{
	class Scene;
	class Camera;
	class Pipeline;
	class Framebuffer;
	class SwapChain;
	class Shader;
	class RendererBatch;
	class Buffer;

	class GeometryRenderPass : public RenderPass
	{
	public:
		GeometryRenderPass(Device& device, 
			Scene& scene, SwapChain& swapChain);
		virtual ~GeometryRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		Scene& _scene;

		unordered_map<type_index, RendererBatch*> _batches;
		Buffer* _instancingBuffer;
	};
}

