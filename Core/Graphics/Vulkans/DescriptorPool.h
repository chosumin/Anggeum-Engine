#pragma once
#include "Device.h"
#include "UniformBuffer.h"
#include "TextureBuffer.h"
#include "StorageBuffer.h"

namespace Core
{
	// ============================================
	// Descriptor Set Type
	// ============================================
	enum class DescriptorSetType : uint32_t
	{
		Global = 0,    // Per-frame: Camera, Lighting
		Pass = 1,      // Per-pass: Shadow maps, Pass-specific data
		Material = 2   // Per-material: Textures, Material properties
	};

	// ============================================
	// Descriptor Set Layout
	// ============================================
	class DescriptorSetLayout
	{
	public:
		DescriptorSetLayout(Device& device);
		DescriptorSetLayout(Device& device, DescriptorSetType type);
		~DescriptorSetLayout();

		VkDescriptorSetLayout& GetDescriptorSetLayout() { return _descriptorSetLayout; }
		DescriptorSetType GetType() const { return _type; }
		uint32_t GetSetIndex() const { return static_cast<uint32_t>(_type); }

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
		VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
		DescriptorSetType _type = DescriptorSetType::Material;
		bool _isFinalized = false;

		vector<UniformBufferLayoutBinding> _uniformBufferBindings;
		vector<TextureBufferLayoutBinding> _textureBufferBindings;
		vector<StorageBufferLayoutBinding> _storageBufferBindings;
	};

	// ============================================
	// Descriptor Pool
	// ============================================
	class DescriptorPool
	{
	public:
		DescriptorPool(Device& device);
		~DescriptorPool();

		void CreatePool(const vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets);
		void Reset();
		
		// Descriptor set วาด็
		VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);
		vector<VkDescriptorSet> AllocateDescriptorSets(
			const vector<VkDescriptorSetLayout>& layouts);
		
		VkDescriptorPool GetHandle() const { return _descriptorPool; }
		
	private:
		Device& _device;
		VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
		uint32_t _maxSets = 0;
	};
}

