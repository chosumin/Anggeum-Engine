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
		static constexpr const char* RT_MAIN_COLOR = "MainColor";

		ParticlePass(Core::Device& device, Core::WorkerThreadManager& workerThreadManager,
			Core::Scene& scene, VkExtent2D extent, VkFormat swapChainFormat,
			VkSampleCountFlagBits msaaSamples);
		virtual ~ParticlePass() override;

		void Initialize();
		virtual void EnsureRenderTargets(Core::RenderFrame& renderFrame) override;
		virtual void Draw(Core::RenderFrame& renderFrame, Core::SyncContext& syncContext, Core::CommandBuffer& commandBuffer, uint32_t imageIndex) override;
	private:
		Core::Scene& _scene;

		VkExtent2D _extent;
		VkFormat _swapChainFormat;
		VkSampleCountFlagBits _msaaSamples;

		vector<Core::Buffer*> _buffers;

		DeltaTime _deltaTime;
		shared_ptr<Core::Material> _computeMaterial;
		unique_ptr<Core::Pipeline> _computePipeline;
		shared_ptr<Core::Material> _graphicsMaterial;
		unique_ptr<Core::Pipeline> _graphicsPipeline;

		bool _initialized = false;
	};
}

