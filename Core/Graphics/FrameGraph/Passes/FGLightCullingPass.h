#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Scene;
	class Device;
	class Pipeline;
	class Buffer;
	class Material;
	class Texture;

	class FGLightCullingPass : public FrameGraphPass
	{
	public:
		FGLightCullingPass(Device& device, Scene& scene,
			VkExtent2D swapChainExtents, ivec2 tileNums, VkSampleCountFlagBits msaaSamples);
		~FGLightCullingPass();

		const char* GetName() const override { return "LightCullingPass"; }
		QueueType GetQueueType() const override { return QueueType::Compute; }
		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		Scene& _scene;
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
