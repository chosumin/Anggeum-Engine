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

		void SetBuffer(uint32_t currentImage, Buffer* data);

		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index, uint32_t binding);
	private:
		vector<Buffer*> _buffers;
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