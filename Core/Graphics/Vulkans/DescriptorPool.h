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
		Shader = 0,      // Per-shader: Lights, Shadows, IBL, Pass-specific data
		Material = 1,    // Per-material: Textures, Material properties
		Bindless = 2     // Global bindless texture array (optional)
	};

	// ============================================
	// Descriptor Set Resources
	// ============================================
	// 단일 descriptor set과 관련된 버퍼를 관리하는 기본 구조체
	struct DescriptorSetResources
	{
		// Descriptor set
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		bool isDescriptorSetUpdated = false;

		// Buffers: [binding] -> Buffer*
		unordered_map<uint32_t, UniformBuffer*> uniformBuffers;
		unordered_map<uint32_t, TextureBuffer*> textureBuffers;
		unordered_map<uint32_t, StorageBuffer*> storageBuffers;

		void CleanupBuffers()
		{
			// Cleanup uniform buffers
			for (auto& [binding, buffer] : uniformBuffers)
			{
				delete buffer;
			}
			uniformBuffers.clear();
			
			// Cleanup texture buffers
			for (auto& [binding, buffer] : textureBuffers)
			{
				delete buffer;
			}
			textureBuffers.clear();
			
			// Cleanup storage buffers
			for (auto& [binding, buffer] : storageBuffers)
			{
				delete buffer;
			}
			storageBuffers.clear();
			
			descriptorSet = VK_NULL_HANDLE;
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
		
		// Descriptor set 할당
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

