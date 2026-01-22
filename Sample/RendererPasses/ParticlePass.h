#pragma once
#include "Graphics/RendererPass.h"

struct Particle
{
	vec2 position;
	vec2 velocity;
	vec4 color;
};

namespace Core
{
	class Pipeline;
	class CommandBuffer;
	class Buffer;
	class Scene;
	class Shader;
	class Material;
	class Mesh;
}

namespace Sample
{
	struct DeltaTime
	{
		float deltaTime = 1.0f;
	};

	class ParticlePass : public Core::RendererPass
	{
	public:
		ParticlePass(Core::Device& device, Core::WorkerThreadManager& workerThreadManager,
			Core::Scene& scene, Core::SwapChain& swapChain,
			shared_ptr<Core::Texture> colorRenderTarget);
		virtual ~ParticlePass() override;

		virtual void Prepare() override;
		virtual void Draw(Core::RenderFrame& renderFrame, uint32_t imageIndex) override;
	private:
		Core::Scene& _scene;

		vector<Core::Buffer*> _buffers;

		DeltaTime _deltaTime;
		shared_ptr<Core::Material> _computeMaterial;
		unique_ptr<Core::Pipeline> _computePipeline;
		shared_ptr<Core::Material> _graphicsMaterial;
		unique_ptr<Core::Pipeline> _graphicsPipeline;
	};
}

