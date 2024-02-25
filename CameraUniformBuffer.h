#pragma once
#include "UniformBuffer.h"

namespace Core
{
	struct VPBufferObject
	{
		alignas(16) mat4 View;
		alignas(16) mat4 Perspective;
	};

	class CameraUniformBuffer : public UniformBuffer
	{
	public:
		CameraUniformBuffer(VkDeviceSize bufferSize, uint32_t binding);
		virtual void MapBufferMemory(void* uniformBufferMapped) override;

		VPBufferObject Matrices;
	};
}

