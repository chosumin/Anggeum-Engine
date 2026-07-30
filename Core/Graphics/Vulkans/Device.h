#pragma once
#include "DebugUtils.h"

namespace Core
{
	struct SwapChainSupportDetails 
	{
		VkSurfaceCapabilitiesKHR Capabilities;
		vector<VkSurfaceFormatKHR> Formats;
		vector<VkPresentModeKHR> PresentModes;
	};

	struct QueueFamilyIndices
	{
		optional<uint32_t> GraphicsFamily;
		optional<uint32_t> ComputeFamily;
		optional<uint32_t> PresentFamily;
		optional<uint32_t> TransferFamily;

		bool IsComplete() { return 
			GraphicsFamily.has_value() && 
			PresentFamily.has_value() &&
			TransferFamily.has_value() &&
			ComputeFamily.has_value(); }
	};

	enum class MemoryType;

	struct DeviceFeatureChain;
	class Window;
	class CommandPool;
	class CommandBuffer;
	class MemoryAllocatorManager;
	class ResourceManager;
	class Device
	{
	public:
		Device(Window& window);
		~Device();

		VkDevice GetDevice() { return _device; }
		SwapChainSupportDetails GetSwapChainSupport() 
		{ 
			return QuerySwapChainSupport(_physicalDevice); 
		}
		VkSurfaceKHR GetSurface() { return _surface; }

		VkQueue GetGraphicsQueue() { return _graphicsQueue; }
		VkQueue GetComputeQueue() { return _computeQueue; }
		VkQueue GetPresentQueue() { return _presentQueue; }
		VkQueue GetTransferQueue() { return _transferQueue; }

		VkPhysicalDevice GetPhysicalDevice() { return _physicalDevice; }
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

		CommandBuffer& BeginSingleTimeCommands() const;
		void EndSingleTimeCommands(CommandBuffer& commandBuffer) const;

		VkFormat FindSupportedFormat(
			const vector<VkFormat>& candidates,
			VkImageTiling tiling,
			VkFormatFeatureFlags features);

		const VkInstance& GetInstance() const { return _instance; }

		MemoryAllocatorManager* GetMemoryAllocatorManager() const;

		const QueueFamilyIndices& GetQueueFamilyIndices() const
		{
			return _queueFamilyIndices;
		}

		ResourceManager& GetResourceManager() const { return *_resourceManager; }

		bool SupportsDescriptorIndexing() const { return _supportsDescriptorIndexing; }

		DebugUtils& GetDebugUtils() { return _debugUtils; }
		const DebugUtils& GetDebugUtils() const { return _debugUtils; }

	private:
		void CreateInstance();
		vector<const char*> GetRequiredExtensions();
		void PickPhysicalDevice(DeviceFeatureChain& outFeatureChain);
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
		bool IsDeviceSuitable(VkPhysicalDevice device, DeviceFeatureChain& outFeatureChain);
		bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
		void CreateLogicalDevice(const DeviceFeatureChain& featureChain);
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);

		void CheckDescriptorIndexingSupport(VkPhysicalDevice device);

	private:
		VkInstance _instance;
		DebugUtils _debugUtils;
		VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
		VkDevice _device;
		
		VkQueue _graphicsQueue;
		VkQueue _computeQueue;
		VkQueue _presentQueue;
		VkQueue _transferQueue;

		ResourceManager* _resourceManager;

		VkSurfaceKHR _surface;

		QueueFamilyIndices _queueFamilyIndices;
		CommandPool* _graphicsCommandPool;

		MemoryAllocatorManager* _memoryAllocatorManager;

		// Descriptor indexing support
		bool _supportsDescriptorIndexing = false;
		VkPhysicalDeviceDescriptorIndexingFeatures _descriptorIndexingFeatures{};

		const vector<const char*> _deviceExtensions;
	};
}

