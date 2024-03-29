#pragma once

namespace Core
{
	class IDescriptor
	{
	public:
		virtual VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding() = 0;
		virtual VkDescriptorType GetDescriptorType() = 0;
	};
}

