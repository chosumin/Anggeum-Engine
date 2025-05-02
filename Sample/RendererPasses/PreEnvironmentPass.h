#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "Foundation/Job.h"

namespace Core
{
	class Scene;
	class Material;
	class Texture;
	class Pipeline;
	class SubMesh;
	class PreEnvironmentPass : public RendererPass
	{
	public:
		PreEnvironmentPass(Device& device, Scene& scene, 
			Texture* renderTarget, Texture* irradianceCubemap, Texture* prefilteredCubemap);
		virtual ~PreEnvironmentPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		void DrawIrradiance(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex);
		void DrawPrefiltered(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex);
	private:
		Scene& _scene;
		
		Texture* _colorRenderTarget;
		SubMesh* _sky;
		Texture* _skyCubemap;
		vector<mat4> _mvpMatrices;

		Texture* _irradianceCubemap;
		Pipeline* _irradiancePipeline;
		Material* _irradianceMaterial;
		IrradianceDelta _delta;

		Texture* _prefilteredCubemap;
		Pipeline* _prefilteredPipeline;
		Material* _prefilteredMaterial;
		PrefilterEnv _prefilterEnv;
	};

	class PreEnvironmentJob : public Job
	{
	public:
		PreEnvironmentJob(PreEnvironmentPass& pass);
		~PreEnvironmentJob();

		void Execute(CommandBuffer& commandBuffer) override;
	private:
		PreEnvironmentPass& _pass;
	};
}