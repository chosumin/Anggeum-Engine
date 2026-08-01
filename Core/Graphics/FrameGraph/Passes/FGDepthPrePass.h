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
		static constexpr const char* RT_MAIN_DEPTH = "MainDepth";
		static constexpr const char* RT_MAIN_NORMAL = "MainNormal";
		static constexpr const char* RT_RESOLVED_DEPTH = "ResolvedDepth";

		FGDepthPrePass(Device& device, Scene& scene, VkExtent2D extent,
			VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);
		~FGDepthPrePass();

		const char* GetName() const override { return "DepthPrePass"; }
		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderExecutor& renderExecutor) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		Scene& _scene;
		VkExtent2D _extent;
		VkSampleCountFlagBits _msaaSamples;

		Handle<Shader> _depthNormalShader;
		unique_ptr<PipelineState> _pipelineState;
		unique_ptr<Pipeline> _pipeline;

		// Refreshed by Setup every frame.
		FGTexture _mainNormal;
		FGTexture _mainDepth;
		FGTexture _resolvedDepth;
		Buffer* _cameraBuffer = nullptr;

		// Prepared on the main thread by Setup (culler creation mutates resource
		// pools); nullptr when there is nothing to draw this frame.
		OcclusionCuller* _culler = nullptr;
	};
}
