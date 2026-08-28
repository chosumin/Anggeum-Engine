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
		// Frame-graph name of cascade i's culled draw list.
		static string IndirectName(uint32_t cascade)
		{
			return "ShadowCull.Cascade" + std::to_string(cascade) + ".Indirect";
		}

		ShadowCullPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene, ShadowPass& shadowPass);
		~ShadowCullPass();

		const char* GetName() const override { return "ShadowCullPass"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		// Cross-frame state, per frame slot (each slot owns its own buffers).
		struct SlotState
		{
			uint64_t batchRevision = 0;
			bool buffersCreated = false;
		};

		Device& _device;
		RenderScene& _renderScene;
		ShadowPass& _shadowPass;

		Handle<Shader> _cullShader;
		Handle<Pipeline> _cullPipeline;
		Handle<Shader> _resetShader;
		Handle<Pipeline> _resetPipeline;

		unordered_map<FrameResources*, SlotState> _slots;

		// Per-frame scratch, set in Setup and consumed by the same frame's
		// Execute. _active gates Execute entirely.
		bool _active = false;
		uint32_t _cascadeCount = 0;
		array<CameraBuffer, SHADOW_MAP_CASCADE_COUNT> _views{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _indirect{};
		array<Handle<Buffer>, SHADOW_MAP_CASCADE_COUNT> _cullData{};
	};
}
