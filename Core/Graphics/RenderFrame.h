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
		void SetShaderTextureBuffer(Shader& shader, uint32_t binding, 
			shared_ptr<Texture> texture, uint mipLevel = 0, 
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		void SetShaderStorageBuffer(Shader& shader, uint32_t binding, Buffer* buffer);

		void SetMaterialBuffers(Material& material);

		BindlessTextureManager* GetBindlessTextureManager() const { return _bindlessTextureManager; }
		bool HasBindlessSupport() const { return _bindlessTextureManager != nullptr; }

		void CleanupBuffers();

		void SetMeshBufferManager(MeshBufferManager* meshBufferManager) { _meshBufferManager = meshBufferManager; }
		MeshBufferManager* GetMeshBufferManager() const { return _meshBufferManager; }

		void SetMaterialManager(MaterialManager* materialManager) { _materialManager = materialManager; }
		MaterialManager* GetMaterialManager() { return _materialManager; }

		shared_ptr<Texture> CreateRenderTarget(const string& name, 
			VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
			VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
			VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

		shared_ptr<Texture> CreateDepthRenderTarget(const string& name,
			VkExtent2D extent, bool isUsedAsSource = true,
			VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

		shared_ptr<Texture> CreateColorRenderTarget(const string& name,
			VkExtent2D extent, VkFormat format, bool isUsedAsSource = false,
			VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

		shared_ptr<Texture> CreateCubemapRenderTarget(const string& name,
			uint32_t size, VkFormat format, uint32_t mipLevels = 1);

		shared_ptr<Texture> GetRenderTarget(const string& name) const;
		bool HasRenderTarget(const string& name) const;
		void RemoveRenderTarget(const string& name);

		void SetPreviousDepthBuffer(shared_ptr<Texture> depth);
		shared_ptr<Texture> GetPreviousDepthBuffer() const { return _previousDepthBuffer; }

		// For debugging purposes
		const unordered_map<string, shared_ptr<Texture>>& GetAllRenderTargets() const { return _renderTargets; }

	private:
		void CreateSyncObjects();
		void CreateDescriptorPool();

		void SetMaterialUniformBuffer(Material& material, uint32_t binding, void* data);
		void SetMaterialTextureBuffer(Material& material, uint32_t binding, 
			shared_ptr<Texture> texture, uint mipLevel,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

		unordered_map<string, shared_ptr<Texture>> _renderTargets;
		shared_ptr<Texture> _previousDepthBuffer;

		shared_ptr<Sampler> _defaultSampler;
	};
}