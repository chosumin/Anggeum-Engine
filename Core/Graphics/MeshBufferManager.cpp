#include "stdafx.h"
#include "MeshBufferManager.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/TransferContext.h"
#include "Graphics/TransferJob.h"

using namespace Core;

MeshBufferManager::MeshBufferManager(Device& device)
	: _device(device)
{
	// Position buffer (vec3)
	_globalPositionBuffer = make_shared<Buffer>(
		_device,
		_maxVertices * sizeof(glm::vec3),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL
	);

	// Normal buffer (vec3)
	_globalNormalBuffer = make_shared<Buffer>(
		_device,
		_maxVertices * sizeof(glm::vec3),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL
	);

	// UV buffer (vec2)
	_globalUVBuffer = make_shared<Buffer>(
		_device,
		_maxVertices * sizeof(glm::vec2),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL
	);

	// Index buffer (uint32)
	_globalIndexBuffer = make_shared<Buffer>(
		_device,
		_maxIndices * sizeof(uint32_t),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL
	);
}

MeshBufferManager::~MeshBufferManager()
{
}

glm::vec4 MeshBufferManager::CalculateBoundingSphere(const vector<glm::vec3>& positions)
{
	if (positions.empty())
		return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

	// AABB 계산
	glm::vec3 min = positions[0];
	glm::vec3 max = positions[0];

	for (const auto& pos : positions)
	{
		min = glm::min(min, pos);
		max = glm::max(max, pos);
	}

	// Bounding sphere center (AABB 중심)
	glm::vec3 center = (min + max) * 0.5f;

	// Bounding sphere radius (가장 먼 정점까지의 거리)
	float radius = 0.0f;
	for (const auto& pos : positions)
	{
		float dist = glm::distance(center, pos);
		radius = glm::max(radius, dist);
	}

	return glm::vec4(center, radius);
}

void MeshBufferManager::CopyBufferToGlobal(Buffer* stagingBuffer, Buffer* dstBuffer, VkDeviceSize offset, VkDeviceSize size)
{
	// TransferContext를 사용하여 비동기 복사
	// TODO: Custom job for buffer copy with offset
}

MeshAllocation MeshBufferManager::AllocateMesh(
	const vector<glm::vec3>& positions,
	const vector<glm::vec3>& normals,
	const vector<glm::vec2>& uvs,
	const vector<uint32_t>& indices)
{
	if (positions.empty() || indices.empty())
		throw runtime_error("Cannot allocate empty mesh");

	if (_currentVertexOffset + positions.size() > _maxVertices)
		throw runtime_error("Global vertex buffer overflow");

	if (_currentIndexOffset + indices.size() > _maxIndices)
		throw runtime_error("Global index buffer overflow");

	MeshAllocation allocation;
	allocation.vertexOffset = _currentVertexOffset;
	allocation.vertexCount = static_cast<uint32_t>(positions.size());
	allocation.indexOffset = _currentIndexOffset;
	allocation.indexCount = static_cast<uint32_t>(indices.size());
	allocation.meshID = _nextMeshID++;

	// Calculate bounding sphere
	glm::vec4 bounds = CalculateBoundingSphere(positions);
	allocation.boundingSphereCenter = glm::vec3(bounds);
	allocation.boundingSphereRadius = bounds.w;

	// TODO: Implement offset-based buffer copy using TransferContext or CommandBuffer

	{
		VkDeviceSize positionSize = positions.size() * sizeof(glm::vec3);
		
		auto stagingBuffer = new Buffer(
			_device,
			positionSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			MemoryType::STAGE
		);
		stagingBuffer->CopyBuffer((void*)positions.data(), positionSize);
		
		// TODO: Copy to global buffer with offset
		
		delete stagingBuffer;
	}

	{
		VkDeviceSize normalSize = normals.size() * sizeof(glm::vec3);
		
		auto stagingBuffer = new Buffer(
			_device,
			normalSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			MemoryType::STAGE
		);
		stagingBuffer->CopyBuffer((void*)normals.data(), normalSize);
		
		// TODO: Copy to global buffer with offset
		
		delete stagingBuffer;
	}

	{
		VkDeviceSize uvSize = uvs.size() * sizeof(glm::vec2);
		
		auto stagingBuffer = new Buffer(
			_device,
			uvSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			MemoryType::STAGE
		);
		stagingBuffer->CopyBuffer((void*)uvs.data(), uvSize);
		
		// TODO: Copy to global buffer with offset
		
		delete stagingBuffer;
	}

	{
		VkDeviceSize indexSize = indices.size() * sizeof(uint32_t);
		
		auto stagingBuffer = new Buffer(
			_device,
			indexSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			MemoryType::STAGE
		);
		stagingBuffer->CopyBuffer((void*)indices.data(), indexSize);
		
		// TODO: Copy to global buffer with offset
		
		delete stagingBuffer;
	}

	// Update offsets
	_currentVertexOffset += allocation.vertexCount;
	_currentIndexOffset += allocation.indexCount;

	// Store allocation
	_allocations[allocation.meshID] = allocation;

	return allocation;
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
	// 1. Compact allocated ranges
	// 2. Update all mesh offsets
	// 3. Re-upload vertex/index data
}

VkBuffer MeshBufferManager::GetPositionBuffer() const
{
	return _globalPositionBuffer->GetBuffer();
}

VkBuffer MeshBufferManager::GetNormalBuffer() const
{
	return _globalNormalBuffer->GetBuffer();
}

VkBuffer MeshBufferManager::GetUVBuffer() const
{
	return _globalUVBuffer->GetBuffer();
}

VkBuffer MeshBufferManager::GetIndexBuffer() const
{
	return _globalIndexBuffer->GetBuffer();
}

const MeshAllocation* MeshBufferManager::GetAllocation(uint32_t meshID) const
{
	auto it = _allocations.find(meshID);
	if (it != _allocations.end())
		return &it->second;
	return nullptr;
}