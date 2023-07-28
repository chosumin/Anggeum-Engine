#pragma once

namespace Core
{
	class Buffer;
	class UniformBuffer
	{
	public:
		UniformBuffer(VkDeviceSize bufferSize);
		~UniformBuffer();

		VkDescriptorSetLayout GetDescriptorSetLayout() { return _descriptorSetLayout; }
		void Update(VkCommandBuffer commandBuffer,
			uint32_t currentImage, VkPipelineLayout pipelineLayout);
	protected:
		virtual void MapBufferMemory(void* uniformBufferMapped) = 0;
	private:
		void CreateDescriptorSetLayout();
		void CreateUniformBuffer();
		void CreateDescriptorPool();
		void CreateDescriptorSets();
	private:
		VkDeviceSize _bufferSize;

		vector<void*> _uniformBuffersMapped;
		VkDescriptorSetLayout _descriptorSetLayout;

		vector<unique_ptr<Buffer>> _buffers;

		VkDescriptorPool _descriptorPool;
		vector<VkDescriptorSet> _descriptorSets;
	};
}

