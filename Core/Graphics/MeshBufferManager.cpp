#include "stdafx.h"
#include "MeshBufferManager.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/TransferContext.h"
#include "Graphics/TransferJob.h"

using namespace Core;

MeshBufferManager::MeshBufferManager(Device& device)
	: _device(device), _indexBuffer(nullptr)
{
}

MeshBufferManager::~MeshBufferManager()
{
	for (auto&& vertexBuffer : _vertexBuffers)
	{
		delete(vertexBuffer.second);
	}
	_vertexBuffers.clear();

	if (_indexBuffer)
		delete(_indexBuffer);
}

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

void MeshBufferManager::FreeMesh(uint32_t meshID)
{
	auto it = _allocations.find(meshID);
	if (it != _allocations.end())
	{
		_freeList.push_back(meshID);
		_allocations.erase(it);
		// TODO: Implement defragmentation
	}
}

void MeshBufferManager::Defragment()
{
	// TODO: Implement buffer defragmentation
}

Buffer* MeshBufferManager::InsertBufferSpace(VkIndexType indexType)
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

	_indexBuffer = new Core::Buffer(_device,
		_maxIndices * size,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	_indexType = indexType;
	return _indexBuffer;
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
	if (_vertexBuffers.find(name) == _vertexBuffers.end())
	{
		_vertexBuffers[name] = new Buffer(_device,
			_maxVertices * stride,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			MemoryType::DEVICE_LOCAL
		);
	}

	// If the data is position, calculate bounding sphere (also accumulates scene bounds)
	if (name == "POSITION")
	{
		// POSITION is tightly packed with the source stride (e.g. 12 bytes for float3),
		// which does not match sizeof(glm::vec3) (16 bytes under GLM_FORCE_DEFAULT_ALIGNED_GENTYPES).
		// Walk the raw buffer using the actual stride and copy only the 3 valid floats per vertex.
		size_t positionCount = data.size() / stride;
		vector<glm::vec3> positionVec(positionCount);

		for (size_t i = 0; i < positionCount; ++i)
		{
			memcpy(&positionVec[i], data.data() + i * stride, sizeof(float) * 3);
		}

		_tempBoundingSphere = CalculateBoundingSphere(positionVec);
	}

	// Update _tempVertexOffset
	uint32_t vertexCount = static_cast<uint32_t>(data.size() / stride);
	_tempVertexOffset = vertexCount;

	VkDeviceSize offset = _currentVertexOffset * stride;

	transferContext.Enqueue(new VkBufferCopyJob<uint8_t>(_device,
		_vertexBuffers[name], move(data), offset), subMeshName + name);
}

void Core::MeshBufferManager::Allocate(TransferContext& transferContext, VkIndexType indexType, std::vector<uint8_t>&& indexData, string subMeshName)
{
	if (_indexBuffer == nullptr)
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

	// Update _tempIndexCount
	uint32_t indexCount = static_cast<uint32_t>(indexData.size() / indexStride);
	_tempIndexCount = indexCount;

	VkDeviceSize offset = _currentIndexOffset * indexStride;

	transferContext.Enqueue(new VkBufferCopyJob<uint8_t>(_device,
		_indexBuffer, move(indexData), offset), subMeshName);
}

MeshAllocation MeshBufferManager::Build()
{
	// Finalize allocations
	MeshAllocation allocation;
	allocation.vertexOffset = _currentVertexOffset;
	allocation.vertexCount = _tempVertexOffset;
	allocation.indexOffset = _currentIndexOffset;
	allocation.indexCount = _tempIndexCount;
	allocation.meshID = _nextMeshID++;
	allocation.boundingSphereCenter = _tempBoundingSphere;
	allocation.boundingSphereRadius = _tempBoundingSphere.w;

	// Update offsets
	_currentVertexOffset += allocation.vertexCount;
	_currentIndexOffset += allocation.indexCount;

	// Store allocation
	_allocations[allocation.meshID] = allocation;

	// Reset temp values
	_tempVertexOffset = 0;
	_tempBoundingSphere = vec4();

	return allocation;
}

vector<Core::Buffer*> MeshBufferManager::GetVertexBuffers(vector<string> names) const
{
	vector<Core::Buffer*> buffers;

	for (string name : names)
	{
		auto vertexBuffer = _vertexBuffers.find(name);
		assert(vertexBuffer != _vertexBuffers.end());

		buffers.push_back(vertexBuffer->second);
	}

	return buffers;
}