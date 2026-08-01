#pragma once
#include "FrameGraphResource.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/SyncContext.h"

namespace Core
{
	class Device;
	class Texture;
	class Buffer;
	class Sampler;
	class Image;

	// Owns the frame graph's transient resources for one frame-in-flight slot:
	// one VkDeviceMemory heap into which VkImages/VkBuffers are placed at offsets
	// chosen so that resources with disjoint pass-lifetime intervals alias the
	// same memory.
	class TransientResourceAllocator
	{
	public:
		struct Request
		{
			string name;
			bool isTexture = true;
			FGTextureDesc texDesc{};
			FGBufferDesc bufDesc{};
			uint32_t firstPass = 0;
			uint32_t lastPass = 0;

			// Queue the resource's passes run on. Aliasing is only legal within
			// one queue: pass indices do not order execution across queues, and
			// the aliasing-activation barrier is a same-queue primitive.
			QueueType queue = QueueType::Graphics;
		};

		struct Placement
		{
			VkDeviceSize offset = 0;
			VkDeviceSize size = 0;
		};

		// Pure placement input, exposed for the self-tests.
		struct PlaceInput
		{
			VkDeviceSize size = 0;
			VkDeviceSize alignment = 1;
			uint32_t firstPass = 0;
			uint32_t lastPass = 0;

			// Memory sharing additionally requires equal queue classes.
			uint32_t queueClass = 0;
		};

		explicit TransientResourceAllocator(Device& device);
		~TransientResourceAllocator();

		TransientResourceAllocator(const TransientResourceAllocator&) = delete;
		TransientResourceAllocator& operator=(const TransientResourceAllocator&) = delete;

		// (Re)creates and places the requested resources. No-op when the request
		// set (descriptions + lifetimes) is unchanged from the previous call.
		// Safe to call per frame.
		void Realize(const vector<Request>& requests, Handle<Sampler> defaultSampler);

		Texture* GetTexture(const string& name) const;
		Buffer* GetBuffer(const string& name) const;

		// Aligned with the requests of the last Realize.
		const vector<Placement>& GetPlacements() const { return _placements; }
		const vector<Request>& GetRequests() const { return _lastRequests; }
		VkDeviceSize GetHeapSize() const { return _heapSize; }

		void Reset();

		u64 GetInvalidateEpoch() const { return _invalidateEpoch; }
		void SetInvalidateEpoch(u64 epoch) { _invalidateEpoch = epoch; }

		// Greedy interval placement: sort by size descending, place each request
		// at the lowest aligned offset where every memory-overlapping placed
		// entry has a disjoint pass interval. Returns the required heap size.
		// Pure — exercised by the synthetic self-tests (FrameGraphSelfTests.cpp).
		static VkDeviceSize Place(const vector<PlaceInput>& inputs, vector<Placement>& outPlacements);

	private:
		void DestroyResources();

		// Creates the unbound VkImages/VkBuffers and fills the placement inputs;
		// returns the intersection of every resource's supported memory types.
		uint32_t CreateUnboundResources(const vector<Request>& requests,
			vector<unique_ptr<Image>>& outImages,
			vector<unique_ptr<Buffer>>& outBuffers,
			vector<PlaceInput>& outInputs);

		// Grow-only heap: reallocates only when the required size exceeds the
		// current heap or the memory type changed.
		void EnsureHeap(VkDeviceSize requiredSize, uint32_t memoryTypeIndex);

		Device& _device;

		VkDeviceMemory _heap = VK_NULL_HANDLE;
		VkDeviceSize _heapSize = 0;
		uint32_t _memoryTypeIndex = UINT32_MAX;
		VkDeviceSize _bufferImageGranularity = 1;

		// Invalidation stamp: When bumping a graph-wide epoch,
		// resets memory lazily on its next realize.
		u64 _invalidateEpoch = 0;

		vector<Request> _lastRequests;
		vector<Placement> _placements;

		unordered_map<string, unique_ptr<Texture>> _textures;
		unordered_map<string, unique_ptr<Buffer>> _buffers;
	};
}
