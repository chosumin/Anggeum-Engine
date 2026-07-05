#pragma once
#include "Vulkans/DescriptorPool.h"
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "IndirectDrawBuffer.h"
#include "Culler.h"

namespace Core
{
	class CommandBuffer;
	class Material;
	class Shader;
	class Texture;
	class Buffer;
	class BindlessTextureManager;
	class IndirectDrawBuffer;
	class RenderPass;
	class Framebuffer;
	class DescriptorSetBuilder;
	class RendererBatches;

	struct RenderTargetDesc
	{
		VkExtent2D extent;
		VkFormat format = VK_FORMAT_UNDEFINED; // For depth targets, this can be left as VK_FORMAT_UNDEFINED to auto-select a suitable depth format
		VkImageUsageFlags usage = 0;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		bool isCubemap = false;
		uint32_t mipLevels = 1;
		uint32_t arrayLayers = 1;
		VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	};

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

		// On-Demand createion
		shared_ptr<Texture> GetOrCreateRenderTarget(const string& name, 
			const RenderTargetDesc& desc);

		shared_ptr<Texture> GetRenderTarget(const string& name) const;

		// Explicit creation (for cases where you want to control the timing of resource creation)
		shared_ptr<Texture> CreateRenderTarget(const string& name,
			const RenderTargetDesc& desc);

		void SetPreviousDepthBuffer(shared_ptr<Texture> depth);
		shared_ptr<Texture> GetPreviousDepthBuffer() const { return _previousDepthBuffer; }

		void SetCurrentDepth(shared_ptr<Texture> depth) { _currentDepth = depth; }
		shared_ptr<Texture> GetCurrentDepth() const { return _currentDepth; }
		void SetCurrentNormal(shared_ptr<Texture> normal) { _currentNormal = normal; }
		shared_ptr<Texture> GetCurrentNormal() const { return _currentNormal; }

		// For debugging purposes
		const unordered_map<string, shared_ptr<Texture>>& GetAllRenderTargets() const { return _renderTargets; }

		Framebuffer* GetOrCreateFramebuffer(const string& name, RenderPass& renderPass,
			const vector<string>& attachmentNames, int32_t layerIndex = -1);
		Framebuffer* GetFramebuffer(const string& name) const;

		void RegisterFramebuffer(const string& name, unique_ptr<Framebuffer> framebuffer);

		// Create a DescriptorSetBuilder for the given shader and set index.
		// The built resources are stored in the frame and cleaned up on Reset().
		DescriptorSetBuilder CreateDescriptorSetBuilder(Shader& shader, uint32_t setIndex = 0);

		// Culler management - per RendererBatches, reused within a frame
		Culler* GetOrCreateCuller(RendererBatches* batch, Device& device, TransformBatch& transformBatch);
		bool IsCullerUsedThisFrame(RendererBatches* batch) const;
		void MarkCullerUsed(RendererBatches* batch);

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
		shared_ptr<Texture> _currentDepth;
		shared_ptr<Texture> _currentNormal;

		shared_ptr<Sampler> _defaultSampler;

		unordered_map<string, unique_ptr<Framebuffer>> _framebuffers;

		// Builder-created resources, cleaned up on Reset()
		vector<DescriptorSetResources> _builderResources;

		// Per-RendererBatches cullers, reused within a frame
		unordered_map<RendererBatches*, unique_ptr<Culler>> _cullers;
	};
}