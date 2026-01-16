#pragma once
#include "UniformBuffer.h"
#include "TextureBuffer.h"
#include "StorageBuffer.h"

namespace Core
{
	class IDescriptor;

	class DescriptorSetLayout
	{
	public:
		DescriptorSetLayout(Device& device);
		~DescriptorSetLayout();

		VkDescriptorSetLayout& GetDescriptorSetLayout() { return _descriptorSetLayout; }

		// Add binding methods
		void AddUniformBufferBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize size);
		void AddTextureBufferBinding(uint32_t binding, VkShaderStageFlags stage);
		void AddStorageBufferBinding(uint32_t binding, VkShaderStageFlags stage);
		
		// Finalize and create Vulkan descriptor set layout
		void Finalize();
		
		// Get binding information
		const vector<UniformBufferLayoutBinding>& GetUniformBufferBindings() const
		{
			return _uniformBufferBindings;
		}
		const vector<TextureBufferLayoutBinding>& GetTextureBufferBindings() const 
		{ 
			return _textureBufferBindings; 
		}
		const vector<StorageBufferLayoutBinding>& GetStorageBufferBindings() const 
		{ 
			return _storageBufferBindings; 
		}
		
	private:
		void CreateDescriptorSetLayout();

	private:
		Device& _device;
		VkDescriptorSetLayout _descriptorSetLayout;
		bool _isFinalized;

		vector<UniformBufferLayoutBinding> _uniformBufferBindings;
		vector<TextureBufferLayoutBinding> _textureBufferBindings;
		vector<StorageBufferLayoutBinding> _storageBufferBindings;
	};
}

