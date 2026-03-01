#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class Scene;
	class SDFGenerator;

	class SDFShadowPass : public RendererPass
	{
	public:
		static constexpr const char* RT_SDF_SHADOW = "SDFShadow";

		SDFShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, VkExtent2D screenExtent);
		~SDFShadowPass();

		void Prepare() override;
		void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

		shared_ptr<Texture> GetSDFShadowTexture() const { return _sdfShadowTexture; }

	private:
		void GenerateSDFVolume(CommandBuffer& commandBuffer);
		void EnsureRenderTargets(RenderFrame& renderFrame);
		void UpdateSDFParams();
		void UpdateGUI();

	private:
		Scene& _scene;
		VkExtent2D _screenExtent;

		unique_ptr<SDFGenerator> _sdfGenerator;
		shared_ptr<Texture> _sdfVolumeTexture;
		shared_ptr<Texture> _sdfShadowTexture;

		shared_ptr<Shader> _sdfShadowShader;
		unique_ptr<Pipeline> _sdfShadowPipeline;

		SDFShadowUniform _sdfParams{};
		bool _sdfGenerated = false;

		// GUI tweakable
		float _shadowSoftness = 8.0f;
		float _minDistance = 0.001f;
		float _maxDistance = 100.0f;
		int _maxSteps = SDF_MAX_MARCH_STEPS;
	};
}