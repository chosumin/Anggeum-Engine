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
			RenderExecutor& renderExecutor) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		Scene& _scene;
		TileInfo _tileInfo;
		VkSampleCountFlagBits _msaaSamples;

		Handle<Material> _computeMaterial;
		unique_ptr<Pipeline> _computePipeline;

		// Refreshed by Setup every frame.
		FGTexture _depth;
		FGBuffer _lightVisibility;

		// Migration bridge: duplicates _depth only because the depth target is a
		// FrameResources-owned import (legacy consumers) and DescriptorSetBuilder
		// wants a pool handle. Goes away when the resource becomes a graph
		// transient/history resource.
		Handle<Texture> _depthHandle;
		Buffer* _lightVisibilityBuffer = nullptr;
		Buffer* _cameraBuffer = nullptr;
		Buffer* _lightBuffer = nullptr;
	};
}
