#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Scene;
	class Device;
	class Shader;
	class Pipeline;
	class PipelineState;
	class Buffer;
	class OcclusionCuller;

	class FGDepthPrePass : public FrameGraphPass
	{
	public:
		enum class Phase { First, Second };

		static constexpr const char* RT_MAIN_DEPTH = "MainDepth";
		static constexpr const char* RT_MAIN_NORMAL = "MainNormal";

		FGDepthPrePass(Device& device, Scene& scene, VkExtent2D extent,
			VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, Phase phase);
		~FGDepthPrePass();

		const char* GetName() const override
		{
			return _phase == Phase::First ? "DepthPre1" : "DepthPre2";
		}
		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		Scene& _scene;
		VkExtent2D _extent;
		VkSampleCountFlagBits _msaaSamples;
		Phase _phase;

		Handle<Shader> _depthNormalShader;
		unique_ptr<PipelineState> _pipelineState;
		unique_ptr<Pipeline> _pipeline;

		FGTexture _mainNormal;
		FGTexture _mainDepth;
		FGBuffer _camera;

		OcclusionCuller* _culler = nullptr;
	};
}
