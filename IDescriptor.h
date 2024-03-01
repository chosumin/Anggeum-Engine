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
}

