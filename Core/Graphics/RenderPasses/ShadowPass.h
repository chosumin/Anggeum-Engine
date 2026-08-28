#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class ResourceManager;
	class Device;
	class RenderScene;
	class Shader;
	class Pipeline;
	class PipelineState;
	class PerspectiveCamera;

	class ShadowPass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_SHADOW_DEPTH = "ShadowDepth";

		ShadowPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene, VkFormat depthFormat);
		~ShadowPass();

		const char* GetName() const override { return "ShadowPass"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;
		void OnGUI(RenderFrame& renderFrame) override;

		// CPU-side shadow block shared with SDFShadowPass (transition distances).
		ShadowUniform* GetShadowBuffer() { return &_shadowBuffer; }

		void UpdateCascades(PerspectiveCamera* camera);
		uint32_t GetCascadeCount() const { return _shadowBuffer.CascadeCount; }
		const CameraBuffer& GetCascadeView(uint32_t cascade) const { return _cascadeViews[cascade]; }

	private:

		/** Returns frustum corners in world space for the given view-projection */
		std::array<glm::vec3, 8> GetFrustumCornersWorldSpace(const glm::mat4& viewProj);

	private:
		Device& _device;
		RenderScene& _renderScene;
		VkExtent2D _shadowExtent;

		Handle<Shader> _shadowShader;
		unique_ptr<PipelineState> _pipelineState;
		unique_ptr<Pipeline> _pipeline;

		ShadowUniform _shadowBuffer;
		array<CameraBuffer, SHADOW_MAP_CASCADE_COUNT> _cascadeViews{};

		FGTexture _shadowDepth;
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _cascadeBuffers{};

		// Per-cascade culled draw lists, produced by ShadowCullPass. Invalid
		// entries mean the cascade was inactive this frame (clear-only).
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _cascadeIndirect{};

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
