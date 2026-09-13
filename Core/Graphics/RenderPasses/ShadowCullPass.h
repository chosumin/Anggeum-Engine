#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class ResourceManager;
	class Device;
	class RenderScene;
	class Shader;
	class Pipeline;
	class FrameResources;
	class ShadowPass;

	class ShadowCullPass : public FrameGraphPass
	{
	public:
		// Frame-graph names of cascade i's compacted draw list.
		static string IndirectName(uint32_t cascade)
		{
			return "ShadowCull.Cascade" + std::to_string(cascade) + ".Indirect";
		}
		static string DrawCountName(uint32_t cascade)
		{
			return "ShadowCull.Cascade" + std::to_string(cascade) + ".DrawCount";
		}
		static string MaterialsName(uint32_t cascade)
		{
			return "ShadowCull.Cascade" + std::to_string(cascade) + ".Materials";
		}
		static string CountsName(uint32_t cascade)
		{
			return "ShadowCull.Cascade" + std::to_string(cascade) + ".Counts";
		}
		static string InstanceIdsName(uint32_t cascade)
		{
			return "ShadowCull.Cascade" + std::to_string(cascade) + ".InstanceIDs";
		}

		ShadowCullPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene, ShadowPass& shadowPass);
		~ShadowCullPass();

		const char* GetName() const override { return "ShadowCullPass"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		RenderScene& _renderScene;
		ShadowPass& _shadowPass;

		Handle<Shader> _cullShader;
		Handle<Pipeline> _cullPipeline;
		Handle<Shader> _compactShader;
		Handle<Pipeline> _compactPipeline;

		bool _active = false;
		uint32_t _cascadeCount = 0;
		array<CameraBuffer, SHADOW_MAP_CASCADE_COUNT> _views{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _instanceCounts{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _instanceIDs{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _indirect{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _visibleMaterials{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _drawCounts{};
		array<Handle<Buffer>, SHADOW_MAP_CASCADE_COUNT> _cullData{};
	};
}
