#include "stdafx.h"
#include "InstanceData.h"
#include "Buffer.h"
#include "Transform.h"
using namespace Core;

Core::InstanceBuffer::InstanceBuffer(Device& device)
{
	_instanceData.resize(_maxInstanceCount);

	_bufferSize = sizeof(_instanceData[0]) * _instanceData.size();

	_instanceBuffer = new Core::Buffer(device, _bufferSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	_instanceBuffer->MapMemory(&_instanceDataMapped, _bufferSize);
}

Core::InstanceBuffer::~InstanceBuffer()
{
	delete(_instanceBuffer);
}

void Core::InstanceBuffer::SetBuffer(size_t index, Transform& transform)
{
	_instanceData[index].World = transform.GetMatrix();
}

void Core::InstanceBuffer::Copy()
{
	//_instanceBuffer->CopyBuffer(_instanceData.data(), _bufferSize);
	
	memcpy(_instanceDataMapped, _instanceData.data(), _bufferSize);
}
