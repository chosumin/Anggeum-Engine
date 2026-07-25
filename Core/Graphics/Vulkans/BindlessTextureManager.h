#pragma once
#include "Device.h"
#include "Texture.h"

namespace Core
{
	// Packed bindless slot index: low 31 bits are the array index, the MSB flags a
	// cubemap (binding 1) vs a 2D texture (binding 0). UINT32_MAX means "none".
	inline constexpr uint32_t InvalidBindlessIndex = UINT32_MAX;
	inline constexpr uint32_t BindlessCubemapFlag = 0x80000000u;

	// Internal texture slot
	struct TextureSlot
	{
		TextureBuffer textureBuffer;
		bool isActive = false;
	};

	class BindlessTextureManager
	{
	public:
		BindlessTextureManager(Device& device, uint32_t maxTextures = 4096);
		~BindlessTextureManager();

		BindlessTextureManager(const BindlessTextureManager&) = delete;
		BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;
		BindlessTextureManager(BindlessTextureManager&&) = delete;
		BindlessTextureManager& operator=(BindlessTextureManager&&) = delete;

		void Initialize();

		// Texture registration (auto-detects 2D vs Cubemap). Takes a Handle<Texture>
		// and returns the packed bindless slot index (see BindlessCubemapFlag).
		// Slot lifetime is driven by the cache that owns the texture, so the slot
		// needs no generation of its own.
		uint32_t RegisterTexture(Handle<Texture> texture);
		void UnregisterTexture(uint32_t bindlessIndex);

		// Descriptor management
		void UpdateDescriptorSet();
		VkDescriptorSet GetDescriptorSet() const { return _descriptorSet; }
		VkDescriptorSetLayout GetDescriptorSetLayout() const { return _descriptorSetLayout; }
		
		static constexpr uint32_t GetSetIndex() { return 2; }

		// Statistics
		uint32_t GetActiveTextureCount() const { return _activeTexture2DCount + _activeCubemapCount; }
		uint32_t GetActive2DTextureCount() const { return _activeTexture2DCount; }
		uint32_t GetActiveCubemapCount() const { return _activeCubemapCount; }
		uint32_t GetMaxTextures() const { return _maxTextures; }
		float GetUsagePercentage() const 
		{ 
			return (GetActiveTextureCount() * 100.0f) / (_maxTextures * 2); 
		}
		
	private:
		void CreateDescriptorSetLayout();
		void CreateDescriptorPool();
		void AllocateDescriptorSet();
		uint32_t AllocateSlot(bool isCubemap);
		void FreeSlot(uint32_t index, bool isCubemap);

	private:
		Device& _device;
		uint32_t _maxTextures;
		uint32_t _activeTexture2DCount = 0;
		uint32_t _activeCubemapCount = 0;

		VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet _descriptorSet = VK_NULL_HANDLE;

		// Separate storage for 2D and Cubemap textures
		vector<TextureSlot> _texture2DSlots;    // Binding 0
		vector<TextureSlot> _cubemapSlots;      // Binding 1
		vector<uint32_t> _freeTexture2DSlots;
		vector<uint32_t> _freeCubemapSlots;
		
		vector<uint32_t> _pendingUpdates;
		bool _needsUpdate = false;
	};
}