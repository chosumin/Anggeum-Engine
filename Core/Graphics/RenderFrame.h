#pragma once

namespace Core
{
	class CommandBuffer;
	class DescriptorPool;
	class RenderFrame
	{
	public:
		RenderFrame(Device& device);
		~RenderFrame();
		
		// Reset frame resources
		void Reset();
		
		// Set command buffers (allocated from RenderContext's CommandPool)
		void SetCommandBuffer(CommandBuffer* commandBuffer) { _commandBuffer = commandBuffer; }
		void SetComputeCommandBuffer(CommandBuffer* computeBuffer) { _computeCommandBuffer = computeBuffer; }
		
		// Getters
		CommandBuffer& GetCommandBuffer() { return *_commandBuffer; }
		CommandBuffer& GetComputeCommandBuffer() { return *_computeCommandBuffer; }
		VkSemaphore GetImageAvailableSemaphore() const { return _imageAvailableSemaphore; }
		VkSemaphore GetRenderFinishedSemaphore() const { return _renderFinishedSemaphore; }
		DescriptorPool& GetDescriptorPool() { return *_descriptorPool; }
	private:
		void CreateSyncObjects();
		void CreateDescriptorPool();
	private:
		Device& _device;
		
		// Command buffers (allocated by RenderContext, not owned)
		CommandBuffer* _commandBuffer = nullptr;
		CommandBuffer* _computeCommandBuffer = nullptr;
		
		// Synchronization objects (owned by RenderFrame)
		VkSemaphore _imageAvailableSemaphore = VK_NULL_HANDLE;
		VkSemaphore _renderFinishedSemaphore = VK_NULL_HANDLE;
		
		// Per-frame descriptor pool (owned by RenderFrame)
		unique_ptr<DescriptorPool> _descriptorPool;
	};
}