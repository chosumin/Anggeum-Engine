#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class RenderScene;
	class Shader;
	class Pipeline;
	class SDFGenerator;

	// Screen-space SDF shadow mask: ray-marches the scene's SDF
	// volume blended with the cascaded shadow maps.
	class SDFShadowPass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_SDF_SHADOW       = "SDFShadow";
		static constexpr const char* RT_SDF_VOLUME_SLICE = "SDFVolumeSlice";

		SDFShadowPass(Device& device, RenderScene& renderScene, VkExtent2D screenExtent,
			VkSampleCountFlagBits msaaSamples, ShadowUniform* shadowBuffer);
		~SDFShadowPass();

		const char* GetName() const override { return "SDFShadowPass"; }
		QueueType GetQueueType() const override { return QueueType::Compute; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;
		void OnGUI(RenderFrame& renderFrame) override;

		SDFGenerator* GetSDFGenerator() const { return _sdfGenerator.get(); }

	private:
		void UpdateSDFParams();

	private:
		struct VolumeRaytracePushConstants
		{
			glm::vec4 cameraPos;
			glm::vec4 cameraForward;
			glm::vec4 cameraRight;
			glm::vec4 cameraUp;
			float fov;
			float maxDistance;
			int32_t maxSteps;
			float hitThreshold;
			float paddingFactor;
		};

		Device& _device;
		RenderScene& _renderScene;
		VkExtent2D _screenExtent;
		VkSampleCountFlagBits _msaaSamples;
		ShadowUniform* _shadowBuffer;

		unique_ptr<SDFGenerator> _sdfGenerator;

		Handle<Shader> _sdfShadowShader;
		Handle<Pipeline> _sdfShadowPipeline;

		// Volume visualization
		Handle<Shader> _volumeSliceShader;
		Handle<Pipeline> _volumeSlicePipeline;
		VkDescriptorSet _sdfShadowImGuiDS = VK_NULL_HANDLE;
		VkDescriptorSet _volumeSliceImGuiDS = VK_NULL_HANDLE;

		FGTexture _sdfShadow;
		FGTexture _volumeSlice;
		FGTexture _depth;
		FGBuffer _camera;
		FGBuffer _sdfParamsBuffer;
		VolumeRaytracePushConstants _slicePushConstants{};
		bool _sliceReady = false;

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
