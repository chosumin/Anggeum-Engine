#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class Shader;
	class Pipeline;
	class Texture;

	// Frame-graph port of ResolvePass: resolves the MSAA depth/normal targets to
	// single-sample images via compute dispatches on the graphics queue.
	class FGResolvePass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_RESOLVED_DEPTH = "ResolvedDepth";
		static constexpr const char* RT_RESOLVED_NORMAL = "ResolvedNormal";

		FGResolvePass(Device& device, VkExtent2D screenExtent, VkSampleCountFlagBits msaaSamples);
		~FGResolvePass();

		const char* GetName() const override { return "ResolvePass"; }
		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderExecutor& renderExecutor) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		VkExtent2D _screenExtent;
		VkSampleCountFlagBits _msaaSamples;

		Handle<Shader> _depthResolveShader;
		unique_ptr<Pipeline> _depthResolvePipeline;
		Handle<Shader> _normalResolveShader;
		unique_ptr<Pipeline> _normalResolvePipeline;

		// Refreshed by Setup every frame.
		FGTexture _mainDepth;
		FGTexture _mainNormal;
		FGTexture _resolvedDepth;
		FGTexture _resolvedNormal;

		// Migration bridge: these duplicate the FGTexture handles above only
		// because the resources are FrameResources-owned imports (legacy passes
		// still consume them by name) and DescriptorSetBuilder wants pool
		// handles. They disappear once every consumer is migrated and the
		// resources become graph transients.
		Handle<Texture> _mainDepthHandle;
		Handle<Texture> _resolvedDepthHandle;
		Handle<Texture> _resolvedNormalHandle;
	};
}
