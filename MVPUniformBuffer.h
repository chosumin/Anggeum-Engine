#pragma once
#include "UniformBuffer.h"

namespace Core
{
	class ModelUniformBuffer : public UniformBuffer
	{
	public:
		ModelUniformBuffer();
	protected:
		void MapBufferMemory(void* uniformBufferMapped) override;
	public:
		mat4 BufferObject;
	};
}

