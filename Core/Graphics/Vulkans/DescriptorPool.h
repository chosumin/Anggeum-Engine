#pragma once
#include "UniformBuffer.h"
#include "StorageBuffer.h"
#include "Texture.h"

namespace Core
{
	// ============================================
	// Descriptor Set Type
	// ============================================
	enum class DescriptorSetType : uint32_t
	{
		Shader = 0,      // Per-shader: Lights, Shadows, IBL, Pass-specific data
		Material = 1,    // Per-material: Textures, Material properties
		Bindless = 2     // Global bindless texture array (optional)
	};

	// ============================================
	// Descriptor Set Resources
	// ============================================
	//
	// A non-owning view of what a descriptor set is bound to.
	//
	// Nothing here owns the memory it points at:
	//  - descriptorSet is owned by the DescriptorPool it was allocated from
	//    (or by BindlessTextureManager, for the bindless set),
	//  - uniformBuffers / storageBuffers point at buffers whose lifetime is
	//    managed by whoever created them.
	// Reset() therefore only drops the references; it never frees anything.
	struct DescriptorSetResources
	{
		// Descriptor set
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		uint32_t setIndex = 0;
		bool isDescriptorSetUpdated = false;

		unordered_map<uint32_t, UniformBuffer> uniformBuffers;
		unordered_map<uint32_t, TextureBuffer> textureBuffers;
		unordered_map<uint32_t, StorageBuffer> storageBuffers;

		// Drop all references. Does not destroy any buffer or descriptor set.
		void Reset()
		{
			uniformBuffers.clear();
			textureBuffers.clear();
			storageBuffers.clear();

			descriptorSet = VK_NULL_HANDLE;
			isDescriptorSetUpdated = false;
		}
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
		void AddTextureBufferBinding(uint32_t binding, VkShaderStageFlags stage, 
	VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
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

