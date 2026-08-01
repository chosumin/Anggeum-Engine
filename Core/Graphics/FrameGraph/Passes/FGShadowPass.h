#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class Scene;
	class Shader;
	class Pipeline;
	class PipelineState;
	class PerspectiveCamera;
	class FrustumCuller;

	// Cascaded shadow maps: renders each active cascade into one layer of the
	// ShadowDepth 2D-array target with frustum-culled indirect draws.
	class FGShadowPass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_SHADOW_DEPTH = "ShadowDepth";

		FGShadowPass(Device& device, Scene& scene, VkFormat depthFormat);
		~FGShadowPass();

		const char* GetName() const override { return "FGShadowPass"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderExecutor& renderExecutor) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;
		void OnGUI(RenderFrame& renderFrame) override;

		// CPU-side shadow block shared with SDFShadowPass (transition distances).
		ShadowUniform* GetShadowBuffer() { return &_shadowBuffer; }

	private:
		void UpdateCascades(PerspectiveCamera* camera);

		/** Returns frustum corners in world space for the given view-projection */
		std::array<glm::vec3, 8> GetFrustumCornersWorldSpace(const glm::mat4& viewProj);

	private:
		Device& _device;
		Scene& _scene;
		VkExtent2D _shadowExtent;

		Handle<Shader> _shadowShader;
		unique_ptr<PipelineState> _pipelineState;
		unique_ptr<Pipeline> _pipeline;

		ShadowUniform _shadowBuffer;
		array<CameraBuffer, SHADOW_MAP_CASCADE_COUNT> _cascadeViews{};

		FGTexture _shadowDepth;
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _cascadeBuffers{};
		array<FrustumCuller*, SHADOW_MAP_CASCADE_COUNT> _cullers{};

		/** Cascade split lambda (0 = uniform, 1 = logarithmic) */
		float _cascadeSplitLambda = 0.95f;

		// Depth bias (adjustable via ImGui)
		float _depthBiasConstant = 1.25f;
		float _depthBiasSlope = 1.75f;
		float _depthBiasClamp = 0.0f;

		// CSM debug view
		std::array<VkDescriptorSet, SHADOW_MAP_CASCADE_COUNT> _csmDescriptorSets{};
		bool _csmDescriptorsCreated = false;
	};
}
