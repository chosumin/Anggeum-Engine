#pragma once
#include "FrameGraphResource.h"
#include "Graphics/SyncContext.h"
#include "Graphics/Vulkans/RenderingSetup.h"

namespace Core
{
	class RenderFrame;
	class CommandBuffer;
	class Texture;
	class Buffer;
	class FrameGraph;
	class FrameGraphBuilder;
	class FrameResources;
	class DescriptorPool;
	class DescriptorSetBuilder;
	class Shader;
	class Device;

	// Per-pass view the graph hands to Execute().
	// This resolves this pass's virtual handles to the physical resources
	// and carries rendering attachements.
	class FrameGraphPassContext
	{
	public:
		static constexpr uint32_t MaxRenderingVariants = 1;

		uint32_t GetImageIndex() const { return _imageIndex; }

		Texture& GetTexture(FGTexture handle) const;
		Buffer& GetBuffer(FGBuffer handle) const;

		// Allocates from this frame's descriptor pool (worker-safe: the pool
		// serializes allocation internally).
		DescriptorSetBuilder CreateDescriptorSetBuilder(Shader& shader, uint32_t setIndex = 0) const;

		RenderFrame& GetRenderFrame() const { return _renderFrame; }

		void BeginRendering(CommandBuffer& commandBuffer, uint32_t variant = 0) const;
		void EndRendering(CommandBuffer& commandBuffer) const;

		bool HasRendering(uint32_t variant = 0) const { return _rendering[variant].valid; }

		VkExtent2D GetRenderArea(uint32_t variant = 0) const;

	private:
		friend class FrameGraph;

		// Only the FrameGraph builds contexts; the references bind them to a
		// frame and to the graph's physical tables for their whole lifetime.
		FrameGraphPassContext(Device& device, DescriptorPool& descriptorPool,
			RenderFrame& renderFrame, uint32_t imageIndex,
			const vector<Texture*>& textures, const vector<Buffer*>& buffers)
			: _device(device)
			, _descriptorPool(descriptorPool)
			, _renderFrame(renderFrame)
			, _imageIndex(imageIndex)
			, _textures(textures)
			, _buffers(buffers)
		{
		}

		// Exactly what Execute-side descriptor building consumes: the frame
		// slot's pool (owned by FrameResources) and the device for the builder.
		Device& _device;
		DescriptorPool& _descriptorPool;
		RenderFrame& _renderFrame;
		uint32_t _imageIndex;

		// Graph-wide physical resolution tables, owned by the FrameGraph.
		const vector<Texture*>& _textures;
		const vector<Buffer*>& _buffers;

		// Which resource indices this pass declared so undeclared accesses fail.
		vector<uint8_t> _declared;

		array<RenderingSetup, MaxRenderingVariants> _rendering;
	};

	// Base class for frame graph passes.
	//
	// Threading contract:
	//  - Setup runs on the main thread every frame, before compile. All resource
	//    declaration AND all pool-mutating work belongs here.
	//  - Execute records into the given command buffer. It may run on a worker
	//    thread; during the recording window the main thread only waits, so
	//    Handle<T>::Get() is safe, but nothing may mutate resource pools.
	class FrameGraphPass
	{
	public:
		virtual ~FrameGraphPass() = default;

		virtual const char* GetName() const = 0;
		virtual QueueType GetQueueType() const { return QueueType::Graphics; }

		virtual void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) = 0;
		virtual void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) = 0;
		virtual void OnGUI(RenderFrame& renderFrame) {}
	};
}
