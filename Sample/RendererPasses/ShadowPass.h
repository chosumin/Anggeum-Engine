#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class Material;
	class PerspectiveCamera;

	class ShadowPass : public RendererPass
	{
	public:
		static constexpr const char* RT_SHADOW_DEPTH = "ShadowDepth";

		ShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, VkFormat depthFormat,
			ShadowUniform& shadowBuffer, TransformBatch& transformBatch);
		~ShadowPass();

		void Prepare() override;
		void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

		ShadowUniform* GetShadowBuffer() { return &_shadowBuffer; }

	private:
		void EnsureRenderTargets(RenderFrame& renderFrame);
		void UpdateCascades(PerspectiveCamera* camera);
		void UpdateGUI(RenderFrame& renderFrame);

		/** Returns frustum corners in world space for the given view-projection */
		std::array<glm::vec3, 8> GetFrustumCornersWorldSpace(const glm::mat4& viewProj);

	private:
		Scene& _scene;
		VkExtent2D _shadowExtent;

		unique_ptr<RendererBatches> _rendererBatches;

		ShadowUniform& _shadowBuffer;
		array<CameraBuffer, SHADOW_MAP_CASCADE_COUNT> _cascadeViews{};
		shared_ptr<Material> _shadowMaterial;

		VkSampleCountFlagBits _msaaSamples;

		/** Cascade split lambda (0 = uniform, 1 = logarithmic) */
		float _cascadeSplitLambda = 0.95f;

		// CSM debug view
		std::array<VkDescriptorSet, SHADOW_MAP_CASCADE_COUNT> _csmDescriptorSets{};
		bool _csmDescriptorsCreated = false;
	};
}

