#include "stdafx.h"
#include "InstanceData.h"
#include "Buffer.h"

Core::InstanceBuffer::InstanceBuffer(Device& device)
{
	_instanceData.resize(_maxInstanceCount);

	_bufferSize = sizeof(_instanceData[0]) * _instanceData.size();

	_instanceBuffer = new Core::Buffer(device, _bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	_instanceBuffer->MapMemory(&_instanceDataMapped, _bufferSize);
}

Core::InstanceBuffer::~InstanceBuffer()
{
	delete(_instanceBuffer);
}

void Core::InstanceBuffer::SetBuffer(uint32_t index, mat4 world)
{
	_instanceData[index].World = world;
}

void Core::InstanceBuffer::Copy()
{
	memcpy(_instanceDataMapped, &_instanceData, _bufferSize);
}
