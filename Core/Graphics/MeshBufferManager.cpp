#include "stdafx.h"
#include "MeshBufferManager.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/TransferContext.h"
#include "Graphics/TransferJob.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/Vulkans/Device.h"

using namespace Core;

MeshBufferManager::MeshBufferManager(Device& device)
	: _device(device)
{
}

MeshBufferManager::~MeshBufferManager() = default;

glm::vec4 MeshBufferManager::CalculateBoundingSphere(const vector<glm::vec3>& positions)
{
	if (positions.empty())
		return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

	glm::vec3 min = positions[0];
	glm::vec3 max = positions[0];

	for (const auto& pos : positions)
	{
		min = glm::min(min, pos);
		max = glm::max(max, pos);
	}

	// Accumulate scene-wide bounds
	_sceneBoundsMin = glm::min(_sceneBoundsMin, min);
	_sceneBoundsMax = glm::max(_sceneBoundsMax, max);

	glm::vec3 center = (min + max) * 0.5f;

	float radius = 0.0f;
	for (const auto& pos : positions)
	{
		float dist = glm::distance(center, pos);
		radius = glm::max(radius, dist);
	}

	return glm::vec4(center, radius);
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

void Core::MeshBufferManager::Allocate(TransferContext& transferContext, const std::string& name, uint32_t stride, std::vector<uint8_t>&& data, string subMeshName)
{
	if (_vertexBufferHandles.find(name) == _vertexBufferHandles.end())
	{
		_vertexBufferHandles[name] = _device.GetResourceCache().LoadBuffer(
			{ _maxVertices * stride,
			  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			  MemoryType::DEVICE_LOCAL },
			"Mesh." + name);
	}

	// If the data is position, calculate bounding sphere (also accumulates scene bounds)
	if (name == "POSITION")
	{
		// POSITION is tightly packed with the source stride (e.g. 12 bytes for float3)
		size_t positionCount = data.size() / stride;
		vector<glm::vec3> positionVec(positionCount);

		for (size_t i = 0; i < positionCount; ++i)
		{
			memcpy(&positionVec[i], data.data() + i * stride, sizeof(float) * 3);
		}

		_tempBoundingSphere = CalculateBoundingSphere(positionVec);
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

	transferContext.Enqueue(new VkBufferCopyJob<uint8_t>(_device,
		_vertexBufferHandles[name].Get(), move(data), offset), subMeshName + name);
}

void Core::MeshBufferManager::Allocate(TransferContext& transferContext, VkIndexType indexType, std::vector<uint8_t>&& indexData, string subMeshName)
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

	transferContext.Enqueue(new VkBufferCopyJob<uint8_t>(_device,
		_indexBufferHandle.Get(), move(indexData), offset), subMeshName);
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
	allocation.boundingSphereCenter = _tempBoundingSphere;
	allocation.boundingSphereRadius = _tempBoundingSphere.w;

	// Store allocation
	_allocations[allocation.meshID] = allocation;

	// Reset per-submesh temp state
	_hasPendingVertexSpan = false;
	_pendingVertexOffset = 0;
	_pendingVertexCount = 0;
	_pendingIndexOffset = 0;
	_pendingIndexCount = 0;
	_tempBoundingSphere = vec4();

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