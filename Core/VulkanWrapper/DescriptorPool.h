#pragma once

namespace Core
{
	class IDescriptor;
	class DescriptorPool
	{
	public:
		DescriptorPool(Device& device, vector<IDescriptor*>& descriptors);
		~DescriptorPool();

		VkDescriptorPool& AllocateDescriptorPool();
		VkDescriptorSetLayout& GetDescriptorSetLayout() { return _descriptorSetLayout; };
	private:
		void CreatePoolCreateInfo(vector<IDescriptor*> descriptors);
		void CreateDescriptorSetLayout(vector<IDescriptor*> descriptors);
	private:
		Device& _device;
		vector<VkDescriptorPool> _descriptorPools;
		VkDescriptorSetLayout _descriptorSetLayout;
		VkDescriptorPoolCreateInfo _poolCreateInfo;
		vector<VkDescriptorPoolSize> _poolSize;
	};
}

