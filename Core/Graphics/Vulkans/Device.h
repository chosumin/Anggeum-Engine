#pragma once

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

	class Window;
	class CommandPool;
	class CommandBuffer;
	class MemoryAllocatorManager;
	class ResourceCache;
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
		QueueFamilyIndices FindQueueFamilies()
		{
			return FindQueueFamilies(_physicalDevice);
		}

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

		ResourceCache& GetResourceCache() const { return *_resourceCache; }

		bool SupportsDescriptorIndexing() const { return _supportsDescriptorIndexing; }

		void SetEnableGpuDrivenRendering(bool enable) { _enableGpuDrivenRendering = enable; }
		bool IsGpuDrivenRenderingEnabled() { return _enableGpuDrivenRendering; }
	private:
		void CreateInstance();
		bool CheckValidationLayerSupport();
		vector<const char*> GetRequiredExtensions();
		void SetupDebugMessenger();
		void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
		void PickPhysicalDevice();
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
		bool IsDeviceSuitable(VkPhysicalDevice device);
		bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
		void CreateLogicalDevice();
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);

		void CheckDescriptorIndexingSupport(VkPhysicalDevice device);

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData)
		{
			std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
			return VK_FALSE;
		}
	private:
		VkInstance _instance;
		VkDebugUtilsMessengerEXT _debugMessenger;
		VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
		VkDevice _device;
		
		VkQueue _graphicsQueue;
		VkQueue _computeQueue;
		VkQueue _presentQueue;
		VkQueue _transferQueue;

		ResourceCache* _resourceCache;

		VkSurfaceKHR _surface;

		QueueFamilyIndices _queueFamilyIndices;
		CommandPool* _graphicsCommandPool;

		MemoryAllocatorManager* _memoryAllocatorManager;

		// Descriptor indexing support
		bool _supportsDescriptorIndexing = false;
		VkPhysicalDeviceDescriptorIndexingFeatures _descriptorIndexingFeatures{};

		bool _enableGpuDrivenRendering = false;

		const vector<const char*> _validationLayers = 
		{
			"VK_LAYER_KHRONOS_validation"
		};

		const vector<const char*> _deviceExtensions =
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME // Added for bindless
		};
#ifdef NDEBUG
		const bool _enableValidationLayers = false;
#else
		const bool _enableValidationLayers = true;
#endif
	};
}

