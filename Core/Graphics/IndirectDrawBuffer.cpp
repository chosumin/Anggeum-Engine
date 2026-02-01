#include "stdafx.h"
#include "IndirectDrawBuffer.h"
#include "MeshBufferManager.h"
using namespace Core;

IndirectDrawBuffer::IndirectDrawBuffer()
{
}

void IndirectDrawBuffer::AddDrawCommand(
	const MeshAllocation& allocation,
	uint32_t materialIndex,
	uint32_t instanceCount,
	uint32_t firstInstance)
{
	DrawIndexedIndirectCommand cmd{};
	cmd.indexCount = allocation.indexCount;
	cmd.instanceCount = instanceCount;
	cmd.firstIndex = allocation.indexOffset;
	cmd.vertexOffset = static_cast<int32_t>(allocation.vertexOffset);
	cmd.firstInstance = firstInstance;

	_drawCommands.push_back(cmd);
	_materialIndices.push_back(materialIndex);
}

void IndirectDrawBuffer::Clear()
{
	_drawCommands.clear();
	_materialIndices.clear();
}