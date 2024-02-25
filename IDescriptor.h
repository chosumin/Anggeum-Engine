#pragma once

namespace Core
{
	class IDescriptor
	{
	public:
		virtual VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding() = 0;
		virtual VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index) = 0;
		virtual VkDescriptorType GetDescriptorType() = 0;
	};

	class Descriptor
	{
	public:
		Descriptor(uint32 binding);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
		void SetDescriptorType(VkDescriptorType type);
	public:
		VkDescriptorBufferInfo BufferInfo;
		VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding{};
		VkWriteDescriptorSet WriteDescriptorSet{};
		VkDescriptorType DescriptorType;
		uint32 Binding;
	};
}

