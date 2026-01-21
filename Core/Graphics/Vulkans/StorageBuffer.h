#pragma once
#include "IDescriptor.h"

namespace Core
{
	class Buffer;
	class StorageBuffer
	{
	public:
		StorageBuffer();
		~StorageBuffer();

		void SetBuffer(Buffer* data);

		VkWriteDescriptorSet CreateWriteDescriptorSet(uint32_t binding);
	private:
		Buffer* _buffer;
		VkDescriptorBufferInfo _bufferInfo;
	};

	struct StorageBufferLayoutBinding : public IDescriptor
	{
	public:
		StorageBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkDescriptorType GetDescriptorType();

		uint32_t Binding;
		VkShaderStageFlags Stage;
	};
}