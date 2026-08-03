#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class RenderScene;
	class Device;
	class Pipeline;
	class Buffer;
	class Material;
	class Texture;

	class LightCullingPass : public FrameGraphPass
	{
	public:
		LightCullingPass(Device& device, RenderScene& renderScene,
			VkExtent2D swapChainExtents, ivec2 tileNums, VkSampleCountFlagBits msaaSamples);
		~LightCullingPass();

		const char* GetName() const override { return "LightCullingPass"; }
		QueueType GetQueueType() const override { return QueueType::Compute; }
		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		RenderScene& _renderScene;
		TileInfo _tileInfo;
		VkSampleCountFlagBits _msaaSamples;

		Handle<Material> _computeMaterial;
		Handle<Pipeline> _computePipeline;

		FGTexture _depth;
		FGBuffer _lightVisibility;
		FGBuffer _camera;
		FGBuffer _lights;
	};
}
