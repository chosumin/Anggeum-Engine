#include "stdafx.h"
#include "MeshBufferManager.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/Vulkans/Device.h"

using namespace Core;

MeshBufferManager::MeshBufferManager(Device& device)
	: _device(device)
{
}

MeshBufferManager::~MeshBufferManager() = default;

MeshAllocation MeshBufferManager::AllocateGeometry(SubMeshGeometry& geometry,
	vector<GeometryCopy>& outCopies)
{
	// One span per submesh, shared across its vertex attributes (see Allocate);
	// Build() commits it.
	for (auto& attr : geometry.attributes)
	{
		auto region = Allocate(attr.name, attr.stride, attr.data);

		// Mark POSITION so the upload job computes the bounds off the main thread.
		uint32_t boundsStride = (attr.name == "POSITION") ? attr.stride : 0;
		outCopies.push_back({ region.destination, move(attr.data), region.offset, boundsStride });
	}

	if (geometry.hasIndex)
	{
		auto region = Allocate(geometry.indexType, geometry.indexData);
		outCopies.push_back({ region.destination, move(geometry.indexData), region.offset });
	}

	return Build();
}

uint32_t MeshBufferManager::AllocateSpan(vector<FreeSpan>& freeSpans, uint32_t& tail,
	uint32_t count, uint32_t maxCount, const char* what)
{
	if (count == 0)
		return tail;

	// First-fit over reclaimed spans.
	for (auto it = freeSpans.begin(); it != freeSpans.end(); ++it)
	{
		if (it->size < count)
			continue;

		uint32_t offset = it->offset;
		if (it->size == count)
			freeSpans.erase(it);
		else
		{
			it->offset += count;
			it->size -= count;
		}
		return offset;
	}

	// Otherwise grow the tail.
	if (tail + count > maxCount)
		throw runtime_error(string("MeshBufferManager: out of ") + what + " space");

	uint32_t offset = tail;
	tail += count;
	return offset;
}

void MeshBufferManager::ReleaseSpan(vector<FreeSpan>& freeSpans, uint32_t& tail,
	uint32_t offset, uint32_t count)
{
	if (count == 0)
		return;

	// Insert keeping the list sorted by offset.
	auto pos = std::lower_bound(freeSpans.begin(), freeSpans.end(), offset,
		[](const FreeSpan& span, uint32_t value) { return span.offset < value; });
	auto inserted = freeSpans.insert(pos, FreeSpan{ offset, count });

	// Coalesce with the previous span.
	if (inserted != freeSpans.begin())
	{
		auto prev = std::prev(inserted);
		if (prev->offset + prev->size == inserted->offset)
		{
			prev->size += inserted->size;
			freeSpans.erase(inserted);
			inserted = prev;
		}
	}

	// Coalesce with the next span.
	auto next = std::next(inserted);
	if (next != freeSpans.end() && inserted->offset + inserted->size == next->offset)
	{
		inserted->size += next->size;
		freeSpans.erase(next);
	}

	// If the tail span is now free, hand it back to the tail so the high-water
	// mark shrinks instead of leaving a permanent free region at the end.
	if (!freeSpans.empty())
	{
		auto& last = freeSpans.back();
		if (last.offset + last.size == tail)
		{
			tail = last.offset;
			freeSpans.pop_back();
		}
	}
}

void MeshBufferManager::FreeMesh(uint32_t meshID)
{
	auto it = _allocations.find(meshID);
	if (it != _allocations.end())
	{
		const MeshAllocation& alloc = it->second;
		ReleaseSpan(_vertexFreeSpans, _vertexTail, alloc.vertexOffset, alloc.vertexCount);
		ReleaseSpan(_indexFreeSpans, _indexTail, alloc.indexOffset, alloc.indexCount);

		_freeList.push_back(meshID);
		_allocations.erase(it);
	}
}

void MeshBufferManager::Defragment()
{
	// TODO: Compaction pass (relocate live spans, patch MeshAllocation offsets).
	// Not needed yet: the free-span list in FreeMesh reuses holes in place.
}

Handle<Buffer> MeshBufferManager::InsertBufferSpace(VkIndexType indexType)
{
	VkDeviceSize size = 0;

	switch (indexType)
	{
		case VK_INDEX_TYPE_UINT16:
			size = sizeof(uint16_t);
			break;
		case VK_INDEX_TYPE_UINT32:
			size = sizeof(uint32_t);
			break;
		default:
			throw runtime_error("Unsupported index type");
	}

	_indexType = indexType;
	_indexBufferHandle = _device.GetResourceCache().LoadBuffer(
		{ _maxIndices * size,
		  VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		  MemoryType::DEVICE_LOCAL },
		"Mesh.Index");
	return _indexBufferHandle;
}

const MeshAllocation* MeshBufferManager::GetAllocation(uint32_t meshID) const
{
	auto it = _allocations.find(meshID);
	if (it != _allocations.end())
		return &it->second;
	return nullptr;
}

Core::MeshBufferRegion Core::MeshBufferManager::Allocate(const std::string& name, uint32_t stride, const std::vector<uint8_t>& data)
{
	if (_vertexBufferHandles.find(name) == _vertexBufferHandles.end())
	{
		_vertexBufferHandles[name] = _device.GetResourceCache().LoadBuffer(
			{ _maxVertices * stride,
			  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			  MemoryType::DEVICE_LOCAL },
			"Mesh." + name);
	}

	// All vertex attributes of one submesh share the same span; reserve it on the
	// first attribute and reuse the offset for the rest (they have equal counts).
	uint32_t vertexCount = static_cast<uint32_t>(data.size() / stride);
	if (!_hasPendingVertexSpan)
	{
		_pendingVertexOffset = AllocateSpan(_vertexFreeSpans, _vertexTail,
			vertexCount, _maxVertices, "vertex");
		_pendingVertexCount = vertexCount;
		_hasPendingVertexSpan = true;
	}

	VkDeviceSize offset = static_cast<VkDeviceSize>(_pendingVertexOffset) * stride;
	return { _vertexBufferHandles[name], offset };
}

Core::MeshBufferRegion Core::MeshBufferManager::Allocate(VkIndexType indexType, const std::vector<uint8_t>& indexData)
{
	if (!_indexBufferHandle.IsValid())
	{
		InsertBufferSpace(indexType);
	}

	uint32_t indexStride = 0;
	switch (indexType)
	{
		case VK_INDEX_TYPE_UINT16:
			indexStride = sizeof(uint16_t);
			break;
		case VK_INDEX_TYPE_UINT32:
			indexStride = sizeof(uint32_t);
			break;
		default:
			throw runtime_error("Unsupported index type");
	}

	_indexType = indexType;

	uint32_t indexCount = static_cast<uint32_t>(indexData.size() / indexStride);
	_pendingIndexOffset = AllocateSpan(_indexFreeSpans, _indexTail,
		indexCount, _maxIndices, "index");
	_pendingIndexCount = indexCount;

	VkDeviceSize offset = static_cast<VkDeviceSize>(_pendingIndexOffset) * indexStride;
	return { _indexBufferHandle, offset };
}

MeshAllocation MeshBufferManager::Build()
{
	// Finalize allocation from the spans reserved during Allocate().
	MeshAllocation allocation;
	allocation.vertexOffset = _pendingVertexOffset;
	allocation.vertexCount = _pendingVertexCount;
	allocation.indexOffset = _pendingIndexOffset;
	allocation.indexCount = _pendingIndexCount;
	allocation.meshID = _nextMeshID++;
	// Bounds are filled in later, by the upload job that scans the POSITION stream.
	allocation.boundingSphereCenter = glm::vec3(0.0f);
	allocation.boundingSphereRadius = 0.0f;

	// Store allocation
	_allocations[allocation.meshID] = allocation;

	// Reset per-submesh temp state
	_hasPendingVertexSpan = false;
	_pendingVertexOffset = 0;
	_pendingVertexCount = 0;
	_pendingIndexOffset = 0;
	_pendingIndexCount = 0;

	return allocation;
}

vector<Core::Handle<Core::Buffer>> MeshBufferManager::GetVertexBuffers(vector<string> names) const
{
	vector<Handle<Buffer>> buffers;

	for (string name : names)
	{
		auto vertexBuffer = _vertexBufferHandles.find(name);
		assert(vertexBuffer != _vertexBufferHandles.end());

		buffers.push_back(vertexBuffer->second);
	}

	return buffers;
}