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

		// Reads a 0/1 override from the named environment variable, defaulting to on
		// in Debug builds and off in Release.
		static bool GetDebugFlag(const char* envName);

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

		// VK_LAYER_KHRONOS_validation. Set DEBUG_VALIDATION=0 to turn this off when
		// running under a tool that injects its own Vulkan layers.
		const bool _enableValidationLayers = GetDebugFlag("DEBUG_VALIDATION");

		// VK_EXT_debug_utils: pass labels and object names for graphics debuggers,
		// plus the messenger that surfaces layer messages.
		const bool _enableDebugUtils = GetDebugFlag("DEBUG_UTILS");
	};
}

