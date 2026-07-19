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
			Scene& scene, VkExtent2D swapChainExtents, ivec2 tileNums);
		virtual ~LightCullingPass() override;

		virtual void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex) override;

		virtual QueueType GetQueueType() const override { return QueueType::Compute; }
	private:
		Scene& _scene;

		TileInfo _tileInfo;

		shared_ptr<Core::Material> _computeMaterial;
		unique_ptr<Core::Pipeline> _computePipeline;
	};
}
