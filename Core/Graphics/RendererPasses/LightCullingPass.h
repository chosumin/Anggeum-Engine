#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class Scene;
	class Pipeline;
	class Buffer;
	class Material;
	class LightCullingPass : public RendererPass
	{
	public:
		LightCullingPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, VkExtent2D swapChainExtents, ivec2 tileNums,
			Buffer* lightVisibilityBuffer);
		virtual ~LightCullingPass() override;

		virtual void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex) override;

		virtual QueueType GetQueueType() const override { return QueueType::Compute; }
	private:
		void UpdateLightBuffer();
	private:
		Scene& _scene;

		Core::Buffer* _lightVisibilityBuffer;
		
		LightBuffer _lightBuffer;
		TileInfo _tileInfo;

		shared_ptr<Core::Material> _computeMaterial;
		unique_ptr<Core::Pipeline> _computePipeline;
	};
}
