#pragma once
#include "IDescriptor.h"

namespace Core
{
	class Buffer;
	class UniformBuffer : public IDescriptor
	{
	public:
		UniformBuffer(VkDeviceSize bufferSize);
		~UniformBuffer();

		void Update(uint32_t currentImage);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
		VkDescriptorType GetDescriptorType();
	protected:
		virtual void MapBufferMemory(void* uniformBufferMapped) = 0;
	private:
		void CreateUniformBuffer();
	private:
		VkDeviceSize _bufferSize;
		vector<void*> _uniformBuffersMapped;
		vector<unique_ptr<Buffer>> _buffers;
		VkDescriptorBufferInfo _bufferInfo;
	};
}

