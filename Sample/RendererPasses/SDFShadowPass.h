#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/RendererBatch.h"

namespace Core
{
	class Scene;
	class SDFGenerator;

	class SDFShadowPass : public RendererPass
	{
	public:
		static constexpr const char* RT_SDF_SHADOW = "SDFShadow";
		static constexpr const char* RT_SDF_RESOLVED_DEPTH = "SDFResolvedDepth";

		SDFShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, VkExtent2D screenExtent,
			VkSampleCountFlagBits msaaSamples);
		~SDFShadowPass();

		void Prepare() override;
		void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

	private:
		void EnsureRenderTargets(RenderFrame& renderFrame);
		void ResolveDepth(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			shared_ptr<Texture> msaaDepth);
		void UpdateSDFParams();
		void UpdateGUI();

	private:
		Scene& _scene;
		VkExtent2D _screenExtent;
		VkSampleCountFlagBits _msaaSamples;

		unique_ptr<SDFGenerator> _sdfGenerator;
		shared_ptr<Texture> _sdfShadowTexture;
		shared_ptr<Texture> _resolvedDepthTexture;

		shared_ptr<Shader> _sdfShadowShader;
		unique_ptr<Pipeline> _sdfShadowPipeline;

		// Depth resolve resources
		shared_ptr<Shader> _depthResolveShader;
		unique_ptr<Pipeline> _depthResolvePipeline;

		SDFShadowUniform _sdfParams{};
		bool _sdfGenerated = false;

		// GUI tweakable
		float _shadowSoftness = 8.0f;
		float _minDistance = 0.001f;
		float _maxDistance = 100.0f;
		int _maxSteps = SDF_MAX_MARCH_STEPS;
	};
}