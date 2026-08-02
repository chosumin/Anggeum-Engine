#include "stdafx.h"
#include "TransientResourceAllocator.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/Texture.h"

using namespace Core;

namespace
{
	VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	bool SameRequest(const TransientResourceAllocator::Request& a,
		const TransientResourceAllocator::Request& b)
	{
		if (a.name != b.name || a.isTexture != b.isTexture ||
			a.firstPass != b.firstPass || a.lastPass != b.lastPass)
			return false;

		if (a.queue != b.queue)
			return false;

		if (a.isTexture)
		{
			return a.texDesc.extent.width == b.texDesc.extent.width &&
				a.texDesc.extent.height == b.texDesc.extent.height &&
				a.texDesc.format == b.texDesc.format &&
				a.texDesc.usage == b.texDesc.usage &&
				a.texDesc.samples == b.texDesc.samples &&
				a.texDesc.aspect == b.texDesc.aspect &&
				a.texDesc.mipLevels == b.texDesc.mipLevels &&
				a.texDesc.arrayLayers == b.texDesc.arrayLayers;
		}
		return a.bufDesc.size == b.bufDesc.size && a.bufDesc.usage == b.bufDesc.usage;
	}
}

TransientResourceAllocator::TransientResourceAllocator(Device& device)
	: _device(device)
{
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
	_bufferImageGranularity = properties.limits.bufferImageGranularity;
}

TransientResourceAllocator::~TransientResourceAllocator()
{
	Reset();
}

void TransientResourceAllocator::Reset()
{
	DestroyResources();

	if (_heap != VK_NULL_HANDLE)
	{
		vkFreeMemory(_device.GetDevice(), _heap, nullptr);
		_heap = VK_NULL_HANDLE;
		_heapSize = 0;
	}

	_lastRequests.clear();
	_placements.clear();
	_memoryTypeIndex = UINT32_MAX;
}

void TransientResourceAllocator::DestroyResources()
{
	_textures.clear();
	_buffers.clear();
}

VkDeviceSize TransientResourceAllocator::Place(const vector<PlaceInput>& inputs,
	vector<Placement>& outPlacements)
{
	outPlacements.assign(inputs.size(), Placement{});

	// Sort by size descending (stable order for determinism on ties).
	vector<size_t> order(inputs.size());
	for (size_t i = 0; i < order.size(); i++)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		return inputs[a].size > inputs[b].size;
	});

	struct Placed
	{
		VkDeviceSize offset, size;
		uint32_t firstPass, lastPass;
		uint32_t queueClass;
	};
	vector<Placed> placedResources;

	VkDeviceSize heapSize = 0;

	for (size_t index : order)
	{
		const auto& input = inputs[index];

		// Candidate offsets: 0 and the aligned end of every placed entry.
		vector<VkDeviceSize> candidates;
		candidates.push_back(0);
		for (const auto& placedResource : placedResources)
			candidates.push_back(AlignUp(placedResource.offset + placedResource.size, input.alignment));
		std::sort(candidates.begin(), candidates.end());

		VkDeviceSize chosen = UINT64_MAX;
		for (VkDeviceSize candidate : candidates)
		{
			const VkDeviceSize begin = candidate;
			const VkDeviceSize end = candidate + input.size;

			bool canPlace = true;
			for (const auto& placedResource : placedResources)
			{
				const bool memoryOverlap = begin < placedResource.offset + placedResource.size && placedResource.offset < end;
				if (!memoryOverlap)
					continue;

				// Sharing memory needs disjoint lifetimes AND the same queue:
				// pass indices only order execution within one queue, and the
				// aliasing-activation barrier cannot reach across queues.
				const bool lifetimeDisjoint =
					input.lastPass < placedResource.firstPass || placedResource.lastPass < input.firstPass;
				const bool sameQueue = input.queueClass == placedResource.queueClass;
				if (!lifetimeDisjoint || !sameQueue)
				{
					canPlace = false;
					break;
				}
			}

			if (canPlace)
			{
				chosen = candidate;
				break;
			}
		}

		assert(chosen != UINT64_MAX);
		outPlacements[index].offset = chosen;
		outPlacements[index].size = input.size;
		placedResources.push_back({ chosen, input.size, input.firstPass, input.lastPass, input.queueClass });
		heapSize = std::max(heapSize, chosen + input.size);
	}

	return heapSize;
}

void TransientResourceAllocator::Realize(const vector<Request>& requests, Handle<Sampler> defaultSampler)
{
	// Unchanged request set: everything stays bound where it is.
	if (requests.size() == _lastRequests.size())
	{
		bool same = true;
		for (size_t i = 0; i < requests.size(); i++)
		{
			if (!SameRequest(requests[i], _lastRequests[i]))
			{
				same = false;
				break;
			}
		}
		if (same)
			return;
	}

	// A raw resource can only be bound once, so any change rebuilds the
	// whole set.
	DestroyResources();
	_placements.clear();
	_lastRequests = requests;

	if (requests.empty())
		return;

	// Create unbound resources and gather memory requirements.
	vector<unique_ptr<Image>> images(requests.size());
	vector<unique_ptr<Buffer>> buffers(requests.size());
	vector<PlaceInput> inputs(requests.size());

	const uint32_t memoryTypeBits = CreateUnboundResources(requests, images, buffers, inputs);

	assert(memoryTypeBits != 0 &&
		"transient resources have no common memory type; split heaps not implemented yet");

	const uint32_t memoryTypeIndex = _device.FindMemoryType(memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Compute aliased placements and (re)allocate the heap they need.
	const VkDeviceSize requiredSize = Place(inputs, _placements);
	EnsureHeap(requiredSize, memoryTypeIndex);

	// Bind at the placed offsets and publish by name.
	for (size_t i = 0; i < requests.size(); i++)
	{
		const auto& request = requests[i];
		if (request.isTexture)
		{
			images[i]->BindMemoryAt(_heap, _placements[i].offset);

			// Name the transient's image/view so a validation leak or barrier
			// error prints "MainColor (transient)" instead of a bare handle.
			const string label = request.name + " (transient)";
			auto& debugUtils = _device.GetDebugUtils();
			debugUtils.SetObjectName(VK_OBJECT_TYPE_IMAGE,
				(uint64_t)images[i]->GetImage(), label.c_str());
			debugUtils.SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW,
				(uint64_t)images[i]->GetOrCreateImageView(0), (label + " View").c_str());

			_textures[request.name] = make_unique<Texture>(request.name,
				std::move(images[i]), defaultSampler);
		}
		else
		{
			buffers[i]->BindMemoryAt(_heap, _placements[i].offset);
			_buffers[request.name] = std::move(buffers[i]);
		}
	}
}

uint32_t TransientResourceAllocator::CreateUnboundResources(const vector<Request>& requests,
	vector<unique_ptr<Image>>& outImages,
	vector<unique_ptr<Buffer>>& outBuffers,
	vector<PlaceInput>& outInputs)
{
	uint32_t memoryTypeBits = ~0u;

	for (size_t i = 0; i < requests.size(); i++)
	{
		const auto& request = requests[i];
		VkMemoryRequirements requirements{};

		if (request.isTexture)
		{
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.extent = { request.texDesc.extent.width, request.texDesc.extent.height, 1 };
			imageInfo.mipLevels = request.texDesc.mipLevels;
			imageInfo.arrayLayers = request.texDesc.arrayLayers;
			imageInfo.format = request.texDesc.format;
			imageInfo.samples = request.texDesc.samples;
			imageInfo.usage = request.texDesc.usage;

			outImages[i] = make_unique<Image>(_device, imageInfo,
				request.texDesc.aspect, VK_IMAGE_VIEW_TYPE_2D, Image::Unbound{});
			requirements = outImages[i]->GetMemoryRequirements();
		}
		else
		{
			outBuffers[i] = make_unique<Buffer>(_device, request.bufDesc.size,
				request.bufDesc.usage, Buffer::Unbound{});
			requirements = outBuffers[i]->GetMemoryRequirements();
		}

		memoryTypeBits &= requirements.memoryTypeBits;

		outInputs[i].size = requirements.size;
		// bufferImageGranularity guards aliasing between linear (buffer) and
		// non-linear (image) resources placed adjacently; folding it into every
		// alignment is conservative but simple.
		outInputs[i].alignment = std::max<VkDeviceSize>(requirements.alignment, _bufferImageGranularity);
		outInputs[i].firstPass = request.firstPass;
		outInputs[i].lastPass = request.lastPass;
		outInputs[i].queueClass = static_cast<uint32_t>(request.queue);
	}

	return memoryTypeBits;
}

void TransientResourceAllocator::EnsureHeap(VkDeviceSize requiredSize, uint32_t memoryTypeIndex)
{
	if (requiredSize <= _heapSize && memoryTypeIndex == _memoryTypeIndex)
		return;

	if (_heap != VK_NULL_HANDLE)
		vkFreeMemory(_device.GetDevice(), _heap, nullptr);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = requiredSize;
	allocInfo.memoryTypeIndex = memoryTypeIndex;

	if (vkAllocateMemory(_device.GetDevice(), &allocInfo, nullptr, &_heap) != VK_SUCCESS)
		throw runtime_error("failed to allocate transient resource heap!");

	_heapSize = requiredSize;
	_memoryTypeIndex = memoryTypeIndex;
}

Texture* TransientResourceAllocator::GetTexture(const string& name) const
{
	auto it = _textures.find(name);
	return it != _textures.end() ? it->second.get() : nullptr;
}

Buffer* TransientResourceAllocator::GetBuffer(const string& name) const
{
	auto it = _buffers.find(name);
	return it != _buffers.end() ? it->second.get() : nullptr;
}

