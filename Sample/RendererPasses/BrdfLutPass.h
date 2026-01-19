#pragma once
#include "Graphics/RendererPass.h"
#include "Foundation/Job.h"

namespace Core
{
	class Material;
	class Texture;
	class Pipeline;
	class CommandBuffer;
	
	class BrdfLutPass : public RendererPass
	{
	public:
		BrdfLutPass(Device& device, WorkerThreadManager& workerThreadManager, Texture* brdfLut);
		virtual ~BrdfLutPass() override;

		virtual void Prepare() override;
		virtual void Draw(RenderFrame& renderFrame, uint32_t frameIndex, uint32_t imageIndex) override;
	private:
		Pipeline* _brdfPipeline;
		Material* _brdfMaterial;
	};

	class BrdfLutJob : public Job
	{
	public:
		BrdfLutJob(Device& device, BrdfLutPass& pass);
		~BrdfLutJob();

		void Execute() override;
		
	private:
		BrdfLutPass& _pass;
		RenderFrame _tempRenderFrame; // Temporary RenderFrame to hold command buffer
	};
}