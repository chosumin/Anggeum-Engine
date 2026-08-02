#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class Shader;
	class Pipeline;
	class Texture;

	class FGResolvePass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_RESOLVED_DEPTH = "ResolvedDepth";
		static constexpr const char* RT_RESOLVED_NORMAL = "ResolvedNormal";

		// resolveNormal also resolves MainNormal into ResolvedNormal (MSAA only).
		FGResolvePass(Device& device, VkExtent2D screenExtent,
			VkSampleCountFlagBits msaaSamples, bool resolveNormal);
		~FGResolvePass();

		const char* GetName() const override
		{
			return _resolveNormal ? "ResolvePass" : "DepthResolvePass";
		}

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		VkExtent2D _screenExtent;
		VkSampleCountFlagBits _msaaSamples;
		bool _resolveNormal;

		Handle<Shader> _depthResolveShader;
		Handle<Pipeline> _depthResolvePipeline;
		Handle<Shader> _normalResolveShader;
		Handle<Pipeline> _normalResolvePipeline;

		FGTexture _mainDepth;
		FGTexture _resolvedDepth;
		FGTexture _mainNormal;
		FGTexture _resolvedNormal;
	};
}
