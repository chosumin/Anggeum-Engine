#pragma once
#include "UniformBuffer.h"

namespace Core
{
	struct ModelBufferObject
	{
		alignas(16) mat4 Model;
	};

	class ModelUniformBuffer : public UniformBuffer
	{
	public:
		ModelUniformBuffer();
	protected:
		void MapBufferMemory(void* uniformBufferMapped) override;
	private:
		ModelBufferObject _bufferObject;
	};

	struct MVPBufferObject
	{
		alignas(16) mat4 Model;
		alignas(16) mat4 View;
		alignas(16) mat4 Proj;
	};

	class MVPUniformBuffer : public UniformBuffer
	{
	public:
		MVPUniformBuffer(float width, float height);
	protected:
		void MapBufferMemory(void* uniformBufferMapped) override;
	private:
		MVPBufferObject _bufferObject;
	};
}

