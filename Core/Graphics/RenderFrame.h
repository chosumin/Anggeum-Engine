#pragma once
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/MemoryAllocator.h"
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "IndirectDrawBuffer.h"
#include "RenderExecutor.h"
#include "Vulkans/SubmitInfo.h"

namespace Core
{
	class CommandBuffer;
	class CommandPool;
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

		// NOT VkImageCreateInfo::initialLayout — the image is always created as
		// UNDEFINED (the spec allows only UNDEFINED/PREINITIALIZED there).
		// Use this for cross-queue targets that a graphics pass may sample before
		// the producing compute pass has ever run, so the validation layer sees a
		// valid layout on the first frame.
		VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	struct StorageBufferDesc
	{
		VkDeviceSize size = 0;
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		MemoryType memoryType = MemoryType::DEVICE_LOCAL;
	};

	class RenderFrame
	{
	public:
		RenderFrame(Device& device, BindlessTextureManager* bindlessManager = nullptr);
		~RenderFrame();
		
		// Reset frame resources
		void Reset();

		// Submit info management
		SubmitInfo& AddSubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext);
		SubmitInfo& GetCurrentSubmitInfo() { return _submission.submitInfos.back(); }
		FrameSubmission& GetSubmission() { return _submission; }

		BindlessTextureManager* GetBindlessTextureManager() const { return _bindlessTextureManager; }
		bool HasBindlessSupport() const { return _bindlessTextureManager != nullptr; }
		DescriptorSetResources* GetBindlessResources();

		void SetMeshBufferManager(MeshBufferManager* meshBufferManager) { _meshBufferManager = meshBufferManager; }
		MeshBufferManager* GetMeshBufferManager() const { return _meshBufferManager; }

		void SetMaterialManager(MaterialManager* materialManager) { _materialManager = materialManager; }
		MaterialManager* GetMaterialManager() { return _materialManager; }

		// On-Demand createion
		shared_ptr<Texture> GetOrCreateRenderTarget(const string& name, 
			const RenderTargetDesc& desc);

		shared_ptr<Texture> GetRenderTarget(const string& name) const;

		// Storage buffers that one pass produces and another consumes within the
		// same frame. Created on first request and reused for the frame's lifetime.
		Buffer& GetOrCreateStorageBuffer(const string& name, const StorageBufferDesc& desc);

		// Uniform data for this frame. Each frame-in-flight owns its own buffer per
		// name. A name identifies one value within a frame and may be shared by any
		// number of passes; T fixes the size, so the producer and every consumer
		// naming the same block necessarily agree on its layout.
		template<typename T>
		Buffer& GetOrCreateUniformBuffer(const string& name)
		{
			static_assert(std::is_trivially_copyable<T>::value,
				"Uniform data must be trivially copyable");

			return GetOrCreateUniformBuffer(name, sizeof(T));
		}

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
		RenderExecutor& GetRenderExecutor() { return *_renderExecutor; }

		// Batch initialization - called once at the start of rendering
		void InitializeBatches(Scene& scene, VkExtent2D extents);
	private:
		// Only reachable through the typed overload, so a block's size always comes
		// from a real C++ type rather than a hand-written byte count.
		Buffer& GetOrCreateUniformBuffer(const string& name, VkDeviceSize size);

		void CreateSyncObjects();
		void CreateDescriptorPool();

	private:
		Device& _device;

		FrameSubmission _submission;

		unique_ptr<DescriptorPool> _descriptorPool;

		BindlessTextureManager* _bindlessTextureManager;
		DescriptorSetResources _bindlessResources;

		// GPU Driven Rendering Buffers
		MeshBufferManager* _meshBufferManager = nullptr;
		MaterialManager* _materialManager = nullptr;

		unordered_map<string, shared_ptr<Texture>> _renderTargets;
		unordered_map<string, unique_ptr<Buffer>> _storageBuffers;
		unordered_map<string, unique_ptr<Buffer>> _uniformBuffers;

		shared_ptr<Texture> _previousDepthBuffer;
		shared_ptr<Texture> _currentDepth;
		shared_ptr<Texture> _currentNormal;

		shared_ptr<Sampler> _defaultSampler;

		unordered_map<string, unique_ptr<Framebuffer>> _framebuffers;

		// Builder-created resources, cleaned up on Reset()
		vector<DescriptorSetResources> _builderResources;

		// Per-camera/RendererBatch cullers, reused within a frame
		unique_ptr<RenderExecutor> _renderExecutor;
	};
}