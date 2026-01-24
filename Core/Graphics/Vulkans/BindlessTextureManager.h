#pragma once
#include "Device.h"
#include "Texture.h"

namespace Core
{
	// Bindless texture handle (opaque type for safety)
	struct TextureHandle
	{
		uint32_t index = UINT32_MAX;
		uint32_t generation = 0; // For handle validation
		
		bool IsValid() const { return index != UINT32_MAX; }
		
		bool operator==(const TextureHandle& other) const 
		{ 
			return index == other.index && generation == other.generation; 
		}
		
		bool operator!=(const TextureHandle& other) const 
		{ 
			return !(*this == other); 
		}
	};

	// Texture slot in the bindless array
	struct TextureSlot
	{
		shared_ptr<Texture> texture;
		uint32_t generation = 0;
		bool isActive = false;
	};

	class BindlessTextureManager
	{
	public:
		BindlessTextureManager(Device& device, uint32_t maxTextures = 4096);
		~BindlessTextureManager();

		// Disable copy, enable move
		BindlessTextureManager(const BindlessTextureManager&) = delete;
		BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;
		BindlessTextureManager(BindlessTextureManager&&) = delete;
		BindlessTextureManager& operator=(BindlessTextureManager&&) = delete;

		// Initialize bindless descriptor set layout and pool
		void Initialize();

		// Texture registration
		TextureHandle RegisterTexture(shared_ptr<Texture> texture);
		void UnregisterTexture(TextureHandle handle);
		void UpdateTexture(TextureHandle handle, shared_ptr<Texture> texture);

		// Descriptor management
		void UpdateDescriptorSet();
		VkDescriptorSet GetDescriptorSet() const { return _descriptorSet; }
		VkDescriptorSetLayout GetDescriptorSetLayout() const { return _descriptorSetLayout; }
		
		// Get set index for bindless textures (always Set 2)
		static constexpr uint32_t GetSetIndex() { return 2; }
		
		// Get texture by handle (for validation)
		shared_ptr<Texture> GetTexture(TextureHandle handle) const;
		
		// Statistics
		uint32_t GetActiveTextureCount() const { return _activeTextureCount; }
		uint32_t GetMaxTextures() const { return _maxTextures; }
		float GetUsagePercentage() const 
		{ 
			return (_activeTextureCount * 100.0f) / _maxTextures; 
		}
		
	private:
		void CreateDescriptorSetLayout();
		void CreateDescriptorPool();
		void AllocateDescriptorSet();
		uint32_t AllocateSlot();
		void FreeSlot(uint32_t index);

	private:
		Device& _device;
		uint32_t _maxTextures;
		uint32_t _activeTextureCount = 0;

		// Global bindless descriptor resources (Set 2)
		VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet _descriptorSet = VK_NULL_HANDLE;

		// Texture storage
		vector<TextureSlot> _textureSlots;
		vector<uint32_t> _freeSlots; // Free slot indices for reuse
		
		// Pending updates
		vector<uint32_t> _pendingUpdates;
		bool _needsUpdate = false;
	};
}