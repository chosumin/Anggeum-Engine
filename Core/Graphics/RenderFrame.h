#pragma once
#include "Vulkans/DescriptorPool.h"
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "IndirectDrawBuffer.h"
#include "Culler.h"
#include "RendererBatch.h"

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
	class RendererBatch;
	class PipelineState;
	class Scene;

	// Key for culler cache: combination of camera pointer and batch pointer
	struct CullerKey
	{
		const CameraBuffer* Camera;
		RendererBatch* Batch;

		bool operator==(const CullerKey& other) const
		{
			return Camera == other.Camera && Batch == other.Batch;
		}
	};

	struct CullerKeyHash
	{
		size_t operator()(const CullerKey& key) const
		{
			size_t h = 0;
			h ^= std::hash<const CameraBuffer*>{}(key.Camera);
			h ^= std::hash<RendererBatch*>{}(key.Batch) << 1;
			return h;
		}
	};

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

		CommandBuffer& GetCommandBuffer() { return *_commandBuffer; }
		CommandBuffer& GetComputeCommandBuffer() { return *_computeCommandBuffer; }

		VkSemaphore GetImageAvailableSemaphore() const { return _imageAvailableSemaphore; }
		VkSemaphore GetRenderFinishedSemaphore() const { return _renderFinishedSemaphore; }

		// Per-shader resources access (set index 0, hash-based)
		DescriptorSetResources& GetOrCreateShaderResources(size_t shaderHash);
		DescriptorSetResources* GetShaderResources(size_t shaderHash);

		BindlessTextureManager* GetBindlessTextureManager() const { return _bindlessTextureManager; }
		bool HasBindlessSupport() const { return _bindlessTextureManager != nullptr; }
		DescriptorSetResources* GetBindlessResources();

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

		// Culler management - per camera and RendererBatch, reused within a frame
		Culler* GetOrCreateCuller(RendererBatch* batch, const CameraBuffer& camera, Device& device, TransformBatch& transformBatch);

		// RendererBatch management - single batch for all meshes
		RendererBatch* GetRendererBatch() const;

		// Batch initialization - called once at the start of rendering
		void InitializeBatches(Scene& scene, VkExtent2D extents);
		bool IsBatchesInitialized() const { return _batchesInitialized; }

		// TransformBatch access
		TransformBatch& GetTransformBatch() { return _transformBatch; }

	private:
		void CreateSyncObjects();
		void CreateDescriptorPool();

	private:
		Device& _device;
		
		CommandBuffer* _commandBuffer = nullptr;
		CommandBuffer* _computeCommandBuffer = nullptr;
		
		VkSemaphore _imageAvailableSemaphore = VK_NULL_HANDLE;
		VkSemaphore _renderFinishedSemaphore = VK_NULL_HANDLE;
		
		unique_ptr<DescriptorPool> _descriptorPool;

		unordered_map<size_t, DescriptorSetResources> _shaderResources;

		BindlessTextureManager* _bindlessTextureManager;
		DescriptorSetResources _bindlessResources;

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

		// Per-camera/RendererBatch cullers, reused within a frame
		unordered_map<CullerKey, unique_ptr<Culler>, CullerKeyHash> _cullers;

		// Single RendererBatch for all meshes
		unique_ptr<RendererBatch> _rendererBatch;
		TransformBatch _transformBatch{};
		bool _batchesInitialized = false;
	};
}