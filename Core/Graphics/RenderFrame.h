#pragma once
#include "Vulkans/DescriptorPool.h"
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "IndirectDrawBuffer.h"

namespace Core
{
	class CommandBuffer;
	class Material;
	class Shader;
	class Texture;
	class Buffer;
	class BindlessTextureManager;
	class IndirectDrawBuffer;

	class RenderFrame
	{
	public:
		RenderFrame(Device& device, BindlessTextureManager* bindlessManager = nullptr);
		~RenderFrame();
		
		// Reset frame resources
		void Reset();
		
		// Set command buffers (allocated from RenderContext's CommandPool)
		void SetCommandBuffer(CommandBuffer* commandBuffer) { _commandBuffer = commandBuffer; }
		void SetComputeCommandBuffer(CommandBuffer* computeBuffer) { _computeCommandBuffer = computeBuffer; }
		
		void AllocateDescriptorSets(Shader& shader);
		void AllocateDescriptorSets(Material& material);
		void AllocateDescriptorSetsWithKey(Shader& shader, size_t key);
		
		// Descriptor set updates
		void UpdateDescriptorSets(Shader& shader);
		void UpdateDescriptorSets(Material& material);
		void UpdateDescriptorSetsWithKey(Shader& shader, size_t key);

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
		void SetShaderTextureBuffer(Shader& shader, uint32_t binding, shared_ptr<Texture> texture, uint mipLevel = 0);
		void SetShaderStorageBuffer(Shader& shader, uint32_t binding, Buffer* buffer);

		void SetMaterialBuffers(Material& material);

		BindlessTextureManager* GetBindlessTextureManager() const { return _bindlessTextureManager; }
		bool HasBindlessSupport() const { return _bindlessTextureManager != nullptr; }

		void CleanupBuffers();

		void SetMeshBufferManager(MeshBufferManager* meshBufferManager) { _meshBufferManager = meshBufferManager; }
		MeshBufferManager* GetMeshBufferManager() const { return _meshBufferManager; }

		void SetMaterialManager(MaterialManager* materialManager) { _materialManager = materialManager; }
		MaterialManager* GetMaterialManager() { return _materialManager; }
	private:
		void CreateSyncObjects();
		void CreateDescriptorPool();

		void SetMaterialUniformBuffer(Material& material, uint32_t binding, void* data);
		void SetMaterialTextureBuffer(Material& material, uint32_t binding, shared_ptr<Texture> texture, uint mipLevel);
		void SetMaterialStorageBuffer(Material& material, uint32_t binding, Buffer* buffer);

	private:
		Device& _device;
		
		CommandBuffer* _commandBuffer = nullptr;
		CommandBuffer* _computeCommandBuffer = nullptr;
		
		VkSemaphore _imageAvailableSemaphore = VK_NULL_HANDLE;
		VkSemaphore _renderFinishedSemaphore = VK_NULL_HANDLE;
		
		unique_ptr<DescriptorPool> _descriptorPool;

		unordered_map<size_t, DescriptorSetResources> _shaderResources;
		unordered_map<string, DescriptorSetResources> _materialResources;

		BindlessTextureManager* _bindlessTextureManager;

		// GPU Driven Rendering Buffers
		MeshBufferManager* _meshBufferManager = nullptr;
		MaterialManager* _materialManager = nullptr;
	};
}