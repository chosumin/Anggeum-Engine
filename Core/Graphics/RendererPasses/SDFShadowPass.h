#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/RendererBatch.h"
#include "ResolvePass.h"

namespace Core
{
	class Scene;
	class SDFGenerator;
	class ShadowPass;

	class SDFShadowPass : public RendererPass
	{
	public:
		static constexpr const char* RT_SDF_SHADOW       = "SDFShadow";
		static constexpr const char* RT_SDF_VOLUME_SLICE = "SDFVolumeSlice";

		SDFShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, VkExtent2D screenExtent,
			VkSampleCountFlagBits msaaSamples, ShadowPass& shadowPass);
		~SDFShadowPass();

		void EnsureRenderTargets(RenderFrame& renderFrame) override;
		void Draw(RenderFrame& renderFrame, SyncContext& syncContext, CommandBuffer& commandBuffer, uint32_t imageIndex) override;
		void OnGUI(RenderFrame& renderFrame) override;

		SDFGenerator* GetSDFGenerator() const { return _sdfGenerator.get(); }

	private:
		void RenderVolumeSlice(RenderFrame& renderFrame, CommandBuffer& commandBuffer);
		void UpdateSDFParams();

	private:
		Scene& _scene;
		VkExtent2D _screenExtent;
		VkSampleCountFlagBits _msaaSamples;
		ShadowPass& _shadowPass;

		unique_ptr<SDFGenerator> _sdfGenerator;
		shared_ptr<Texture> _sdfShadowTexture;

		shared_ptr<Shader> _sdfShadowShader;
		unique_ptr<Pipeline> _sdfShadowPipeline;

		// Volume visualization
		shared_ptr<Shader> _volumeSliceShader;
		unique_ptr<Pipeline> _volumeSlicePipeline;
		shared_ptr<Texture> _volumeSliceTexture;
		VkDescriptorSet _sdfShadowImGuiDS = VK_NULL_HANDLE;
		VkDescriptorSet _volumeSliceImGuiDS = VK_NULL_HANDLE;

		// GPU bounds data (from GeometryPass's RendererBatch)
		Buffer* _objectDataBuffer = nullptr;
		Buffer* _drawCommandBuffer = nullptr;
		uint32_t _drawCommandCount = 0;
		uint32_t _instanceCount = 0;

		SDFShadowUniform _sdfParams{};

		// Persistent SDF cache
		bool _regenerateRequested = false;
		bool _savePending = false;

		// GUI tweakable
		float _shadowSoftness = 8.0f;
		float _minDistance = 0.001f;
		float _maxDistance = 100.0f;
		int _maxSteps = SDF_MAX_MARCH_STEPS;

		// Volume raytrace debug
		float _debugHitThreshold = 0.01f;
		int _debugMaxSteps = 128;
	};
}
