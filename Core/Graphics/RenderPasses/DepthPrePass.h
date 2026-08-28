#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class ResourceManager;
	class RenderScene;
	class Device;
	class Shader;
	class Pipeline;
	class PipelineState;
	class Buffer;

	class DepthPrePass : public FrameGraphPass
	{
	public:
		enum class Phase { First, Second };

		static constexpr const char* RT_MAIN_DEPTH = "MainDepth";
		static constexpr const char* RT_MAIN_NORMAL = "MainNormal";

		DepthPrePass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene, VkExtent2D extent,
			VkFormat depthFormat, VkSampleCountFlagBits msaaSamples, Phase phase);
		~DepthPrePass();

		const char* GetName() const override
		{
			return _phase == Phase::First ? "DepthPre1" : "DepthPre2";
		}
		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		RenderScene& _renderScene;
		VkExtent2D _extent;
		VkSampleCountFlagBits _msaaSamples;
		Phase _phase;

		Handle<Shader> _depthNormalShader;
		unique_ptr<PipelineState> _pipelineState;
		unique_ptr<Pipeline> _pipeline;

		FGTexture _mainNormal;
		FGTexture _mainDepth;
		FGBuffer _camera;

		// This phase's culled draw list, produced by the HiZCull passes.
		FGBuffer _indirect;
	};
}
