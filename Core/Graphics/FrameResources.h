#pragma once
#include "Vulkans/MemoryAllocator.h"
#include "Vulkans/Buffer.h"
#include "ResourcePool.h"

namespace Core
{
	class Device;
	class Texture;
	class Framebuffer;
	class RenderPass;
	class Sampler;
	class Shader;
	class DescriptorPool;
	class DescriptorSetBuilder;
	class CommandBuffer;
	class Job;

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

		// Optional custom sampler. Leave invalid to use default sampler
		Handle<Sampler> sampler;

		// NOT VkImageCreateInfo::initialLayout — the image is always created as
		// UNDEFINED (the spec allows only UNDEFINED/PREINITIALIZED there).
		// Use this for cross-queue targets that a graphics pass may sample before
		// the producing compute pass has ever run, so the validation layer sees a
		// valid layout on the first frame.
		VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	// Owns the per-frame-in-flight GPU resources: render targets, transient
	// storage/uniform buffers, and framebuffers. Split out of RenderFrame so the
	// frame object keeps only execution/sync concerns (submission, descriptor pool,
	// bindless, RenderExecutor). Reach it via RenderFrame::GetResources().
	class FrameResources
	{
	public:
		explicit FrameResources(Device& device);
		~FrameResources();

		FrameResources(const FrameResources&) = delete;
		FrameResources& operator=(const FrameResources&) = delete;

		// Clears per-frame transient selectors (current depth/normal). Render
		// targets and buffers persist and are reused across the frame's lifetime.
		void Reset();

		// On-Demand creation. Render targets are pool-owned per frame and referred
		// to by handle, like cache textures.
		Handle<Texture> GetOrCreateRenderTarget(const string& name,
			const RenderTargetDesc& desc);
		Handle<Texture> GetRenderTarget(const string& name) const;

		// Explicit creation (for cases where you want to control the timing of resource creation)
		Handle<Texture> CreateRenderTarget(const string& name,
			const RenderTargetDesc& desc);

		// Storage buffers that one pass produces and another consumes within the
		// same frame. Created on first request and reused for the frame's lifetime.
		// Pool-owned; resolve the handle with handle.Get().
		Handle<Buffer> GetOrCreateStorageBuffer(const string& name, const BufferDesc& desc);

		// Same, for a buffer that has to start out holding something.
		template<typename T>
		Handle<Buffer> GetOrCreateStorageBuffer(const string& name, const BufferDesc& desc,
			const vector<T>& initialData)
		{
			static_assert(std::is_trivially_copyable<T>::value,
				"Buffer contents must be trivially copyable");

			const auto* bytes = reinterpret_cast<const uint8_t*>(initialData.data());
			return GetOrCreateFilledStorageBuffer(name, desc,
				vector<uint8_t>(bytes, bytes + initialData.size() * sizeof(T)));
		}

		// Uniform data for this frame. Each frame-in-flight owns its own buffer per
		// name. A name identifies one value within a frame and may be shared by any
		// number of passes; T fixes the size, so the producer and every consumer
		// naming the same block necessarily agree on its layout. Pool-owned.
		template<typename T>
		Handle<Buffer> GetOrCreateUniformBuffer(const string& name)
		{
			static_assert(std::is_trivially_copyable<T>::value,
				"Uniform data must be trivially copyable");

			return GetOrCreateUniformBuffer(name, sizeof(T));
		}

		Framebuffer* GetOrCreateFramebuffer(const string& name, RenderPass& renderPass,
			const vector<string>& attachmentNames, int32_t layerIndex = -1);
		Framebuffer* GetFramebuffer(const string& name) const;
		void RegisterFramebuffer(const string& name, unique_ptr<Framebuffer> framebuffer);

		// Create a DescriptorSetBuilder for the given shader and set index. It
		// allocates from this frame's descriptor pool, which Reset() recycles.
		DescriptorSetBuilder CreateDescriptorSetBuilder(Shader& shader, uint32_t setIndex = 0);

		// One-off GPU work a resource needs before the frame's passes can touch it.
		// Creating a resource therefore costs no submit and no fence wait of its own.
		bool HasPendingInit() const;
		void ExecutePendingInit(CommandBuffer& commandBuffer);

		void SetPreviousDepthBuffer(Handle<Texture> depth) { _previousDepthBuffer = depth; }
		Handle<Texture> GetPreviousDepthBuffer() const { return _previousDepthBuffer; }

		void SetCurrentDepth(Handle<Texture> depth) { _currentDepth = depth; }
		Handle<Texture> GetCurrentDepth() const { return _currentDepth; }
		void SetCurrentNormal(Handle<Texture> normal) { _currentNormal = normal; }
		Handle<Texture> GetCurrentNormal() const { return _currentNormal; }

	private:
		// Only reachable through the typed overload, so a block's size always comes
		// from a real C++ type rather than a hand-written byte count.
		Handle<Buffer> GetOrCreateUniformBuffer(const string& name, VkDeviceSize size);

		// Type-erased half of the templated overload above, so the job types stay out
		// of this header.
		Handle<Buffer> GetOrCreateFilledStorageBuffer(const string& name,
			const BufferDesc& desc, vector<uint8_t>&& initialData);

		void CreateDescriptorPool();

		bool AllInitJobsExecuted() const;

	private:
		Device& _device;

		unique_ptr<DescriptorPool> _descriptorPool;

		ResourcePool<Texture> _renderTargetPool;
		unordered_map<string, Handle<Texture>> _renderTargets;

		// Transient per-frame buffers are pool-owned (handle pattern) so they can be
		// resized/relocated in place later via ResourcePool::Replace.
		ResourcePool<Buffer> _bufferPool;
		unordered_map<string, Handle<Buffer>> _storageBufferHandles;
		unordered_map<string, Handle<Buffer>> _uniformBufferHandles;

		Handle<Texture> _previousDepthBuffer;
		Handle<Texture> _currentDepth;
		Handle<Texture> _currentNormal;

		Handle<Sampler> _defaultSampler;

		unordered_map<string, unique_ptr<Framebuffer>> _framebuffers;

		// UNDEFINED -> requested starting layout for targets created this frame.
		// Cleared as they are recorded.
		struct PendingLayoutTransition
		{
			Handle<Texture> texture;
			VkImageLayout targetLayout;
		};
		vector<PendingLayoutTransition> _pendingTransitions;

		// This frame's init jobs, from enqueue until the GPU is done with them: an
		// executed job still owns the staging buffer its copy reads from, so it
		// outlives the recording. Released by Reset(), which only runs once this
		// frame slot's previous submission has completed.
		vector<unique_ptr<Job>> _pendingInitJobs;
	};
}
