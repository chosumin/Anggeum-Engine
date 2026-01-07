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
			shared_ptr<Texture> depthPrepassRenderTarget,
			Buffer* lightVisibilityBuffer);
		virtual ~LightCullingPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		void UpdateLightBuffer();
	private:
		Scene& _scene;

		Core::Buffer* _lightVisibilityBuffer;
		shared_ptr<Texture> _depthPrepassRenderTarget;
		
		LightBuffer _lightBuffer;
		TileInfo _tileInfo;

		shared_ptr<Core::Material> _computeMaterial;
		unique_ptr<Core::Pipeline> _computePipeline;
	};
}