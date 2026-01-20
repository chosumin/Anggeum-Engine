#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "Foundation/Job.h"

namespace Core
{
	class Scene;
	class Texture;
	class CommandBuffer;
	class Pipeline;
	class SubMesh;
	class PreEnvironmentPass : public RendererPass
	{
	public:
		PreEnvironmentPass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene, 
			Texture* renderTarget, Texture* irradianceCubemap, Texture* prefilteredCubemap);
		virtual ~PreEnvironmentPass() override;

		virtual void Prepare() override;
		virtual void Draw(RenderFrame& renderFrame, uint32_t frameIndex, uint32_t imageIndex) override;
	private:
		void DrawIrradiance(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex);
		void DrawPrefiltered(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex);
	private:
		Scene& _scene;
		
		Texture* _colorRenderTarget;
		shared_ptr<SubMesh> _sky;
		vector<mat4> _mvpMatrices;

		Texture* _irradianceCubemap;
		Pipeline* _irradiancePipeline;
		shared_ptr<Material> _irradianceMaterial;
		IrradianceDelta _delta;

		Texture* _prefilteredCubemap;
		Pipeline* _prefilteredPipeline;
		shared_ptr<Material> _prefilteredMaterial;
		PrefilterEnv _prefilterEnv;
		shared_ptr<Texture> _skyCubemap;
	};

	class PreEnvironmentJob : public Job
	{
	public:
		PreEnvironmentJob(Device& device, PreEnvironmentPass& pass);
		~PreEnvironmentJob();
		
		void Execute() override;
		
	private:
		RenderFrame _tempRenderFrame; // Temporary RenderFrame to hold command buffer
		PreEnvironmentPass& _pass;
	};
}