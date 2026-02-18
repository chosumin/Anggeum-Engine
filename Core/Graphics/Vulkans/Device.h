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

		// Debug utils function pointers
		PFN_vkCmdBeginDebugUtilsLabelEXT GetCmdBeginDebugUtilsLabelFunc() const { return _vkCmdBeginDebugUtilsLabel; }
		PFN_vkCmdEndDebugUtilsLabelEXT GetCmdEndDebugUtilsLabelFunc() const { return _vkCmdEndDebugUtilsLabel; }
		PFN_vkCmdInsertDebugUtilsLabelEXT GetCmdInsertDebugUtilsLabelFunc() const { return _vkCmdInsertDebugUtilsLabel; }
		PFN_vkSetDebugUtilsObjectNameEXT GetSetDebugUtilsObjectNameFunc() const { return _vkSetDebugUtilsObjectName; }

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
		void LoadDebugUtilsFunctions();

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

		// Debug utils function pointers
		PFN_vkCmdBeginDebugUtilsLabelEXT _vkCmdBeginDebugUtilsLabel = nullptr;
		PFN_vkCmdEndDebugUtilsLabelEXT _vkCmdEndDebugUtilsLabel = nullptr;
		PFN_vkCmdInsertDebugUtilsLabelEXT _vkCmdInsertDebugUtilsLabel = nullptr;
		PFN_vkSetDebugUtilsObjectNameEXT _vkSetDebugUtilsObjectName = nullptr;

		const vector<const char*> _validationLayers = 
		{
			"VK_LAYER_KHRONOS_validation"
		};

		const vector<const char*> _deviceExtensions;
#ifdef NDEBUG
		const bool _enableValidationLayers = false;
#else
		const bool _enableValidationLayers = true;
#endif
	};
}

