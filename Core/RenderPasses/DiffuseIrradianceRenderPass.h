#pragma once
#include "VulkanWrapper/RenderPass.h"
#include "BufferObjects/BufferObjects.h"

namespace Core
{
	class Scene;
	class Material;
	class Texture;
	class Pipeline;
	class SubMesh;
	class IrradianceRenderPass : public RenderPass
	{
	public:
		IrradianceRenderPass(Device& device, Scene& scene, 
			Texture* renderTarget, Texture* irradianceCubemap);
		virtual ~IrradianceRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		Scene& _scene;
		Texture* _irradianceCubemap;
		Pipeline* _skyboxPipeline;
		Material* _material;
		SubMesh* _sky;
		Texture* _colorRenderTarget;
		Texture* _skyCubemap;
		vector<mat4> _mvpMatrices;
		IrradianceDelta _delta;
	};
}