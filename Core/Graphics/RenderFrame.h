#pragma once
#include "Vulkans/DescriptorPool.h"

namespace Core
{
	class CommandBuffer;
	class UniformBuffer;
	class TextureBuffer;
	class StorageBuffer;
	class Material;
	class Shader;
	class Texture;
	class Buffer;

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
		
		void AllocateDescriptorSets(Shader& shader);
		void AllocateDescriptorSets(Material& material);
		
		// Descriptor set updates
		void UpdateDescriptorSets(Shader& shader);
		void UpdateDescriptorSets(Material& material);

		CommandBuffer& GetCommandBuffer() { return *_commandBuffer; }
		CommandBuffer& GetComputeCommandBuffer() { return *_computeCommandBuffer; }

		VkSemaphore GetImageAvailableSemaphore() const { return _imageAvailableSemaphore; }
		VkSemaphore GetRenderFinishedSemaphore() const { return _renderFinishedSemaphore; }

		// Per-shader resources access (set index 0, hash-based)
		DescriptorSetResources& GetOrCreateShaderResources(size_t shaderHash);
		DescriptorSetResources* GetShaderResources(size_t shaderHash);

		// Per-material resources access (set index 1, name-based)
		DescriptorSetResources& GetOrCreateMaterialResources(const string& materialName);
		DescriptorSetResources* GetMaterialResources(const string& materialName);

		// Per-shader buffer management (set index 0)
		void SetShaderUniformBuffer(Shader& shader, uint32_t binding, void* data);
		void SetShaderTextureBuffer(Shader& shader, uint32_t binding, shared_ptr<Texture> texture);
		void SetShaderStorageBuffer(Shader& shader, uint32_t binding, Buffer* buffer);

		void SetMaterialBuffers(Material& material);

		// Cleanup all buffers
		void CleanupBuffers();
	private:
		void CreateSyncObjects();
		void CreateDescriptorPool();

		// Per-material buffer management (set index 1)
		void SetMaterialUniformBuffer(Material& material, uint32_t binding, void* data);
		void SetMaterialTextureBuffer(Material& material, uint32_t binding, shared_ptr<Texture> texture);
		void SetMaterialStorageBuffer(Material& material, uint32_t binding, Buffer* buffer);
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

		// Per-shader resources (set index = 0, hash-based)
		unordered_map<size_t, DescriptorSetResources> _shaderResources;

		// Per-material resources (set index = 1, name-based)
		unordered_map<string, DescriptorSetResources> _materialResources;
	};
}