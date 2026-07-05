#include "stdafx.h"
#include "Device.h"
#include "CommandPool.h"
#include "CommandBuffer.h"
#include "MemoryAllocator.h"
#include "Graphics/ResourceCache.h"

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr
    (instance, "vkCreateDebugUtilsMessengerEXT");

    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr
    (instance, "vkDestroyDebugUtilsMessengerEXT");

    if (func != nullptr)
        func(instance, debugMessenger, pAllocator);
}

namespace Core
{
	Device::Device(Window& window)
		:_device(), _debugMessenger(), _graphicsQueue(), _presentQueue(), _instance(), _surface(), _computeQueue(),
		_deviceExtensions{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
		}
	{
	    CreateInstance();
	    SetupDebugMessenger();
	    window.CreateSurface(_instance, &_surface);
	    PickPhysicalDevice();
	    CreateLogicalDevice();

	    _queueFamilyIndices = FindQueueFamilies();
	    
	    _graphicsCommandPool = new CommandPool(*this, 
	        _queueFamilyIndices.GraphicsFamily.value());

	    _memoryAllocatorManager = new MemoryAllocatorManager(*this);

	    _resourceCache = new ResourceCache(*this);

		// Load debug utils functions
		LoadDebugUtilsFunctions();
	}

	Device::~Device()
	{
	    delete(_resourceCache);
	    delete(_memoryAllocatorManager);
	    delete(_graphicsCommandPool);

	    vkDestroyDevice(_device, nullptr);

	    if (_enableValidationLayers)
	        DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);

	    vkDestroySurfaceKHR(_instance, _surface, nullptr);
	    vkDestroyInstance(_instance, nullptr);
	}

	void Device::LoadDebugUtilsFunctions()
	{
		// Load from instance, not device
		_vkCmdBeginDebugUtilsLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(_instance, "vkCmdBeginDebugUtilsLabelEXT");
		_vkCmdEndDebugUtilsLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(_instance, "vkCmdEndDebugUtilsLabelEXT");
		_vkCmdInsertDebugUtilsLabel = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(_instance, "vkCmdInsertDebugUtilsLabelEXT");
		_vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(_instance, "vkSetDebugUtilsObjectNameEXT");

		bool supportsDebugUtils = (_vkCmdBeginDebugUtilsLabel != nullptr) &&
			(_vkCmdEndDebugUtilsLabel != nullptr) &&
			(_vkCmdInsertDebugUtilsLabel != nullptr) &&
			(_vkSetDebugUtilsObjectName != nullptr);

		if (supportsDebugUtils)
		{
			cout << "Debug Utils extension supported!" << endl;
		}
		else
		{
			cout << "Warning: Debug Utils extension not fully supported." << endl;
		}
	}

	uint32_t Device::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
	{
	    VkPhysicalDeviceMemoryProperties memProperties;
	    vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProperties);

	    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	    {
	        if ((typeFilter & (1 << i)) &&
	            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
	        {
	            return i;
	        }
	    }

	    throw runtime_error("failed to find suitable memory type!");
	}

	CommandBuffer& Device::BeginSingleTimeCommands() const
	{
	    auto& commandBuffer = _graphicsCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	    commandBuffer.BeginCommandBuffer(true);

	    return commandBuffer;
	}

	void Device::EndSingleTimeCommands(CommandBuffer& commandBuffer) const
	{
	    commandBuffer.EndCommandBuffer();
	    
	    VkSubmitInfo submitInfo{};
	    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	    submitInfo.commandBufferCount = 1;
	    submitInfo.pCommandBuffers = &commandBuffer.GetHandle();

	    VkFenceCreateInfo fence_info{};
	    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	    fence_info.flags = 0;

	    VkFence fence;
	    vkCreateFence(_device, &fence_info, nullptr, &fence);

	    VkResult result = vkQueueSubmit(_graphicsQueue, 1, &submitInfo, fence);
	    vkWaitForFences(_device, 1, &fence, VK_TRUE, 100000000000);

	    vkDestroyFence(_device, fence, nullptr);
	}

	VkFormat Device::FindSupportedFormat(
	    const vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
	{
	    for (VkFormat format : candidates)
	    {
	        VkFormatProperties props;
	        vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &props);

	        if (tiling == VK_IMAGE_TILING_LINEAR &&
	            (props.linearTilingFeatures & features) == features)
	        {
	            return format;
	        }
	        else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
	            (props.optimalTilingFeatures & features) == features)
	        {
	            return format;
	        }
	    }

	    throw runtime_error("failed to find supported format!");
	}

	MemoryAllocatorManager* Device::GetMemoryAllocatorManager() const
	{
	    return _memoryAllocatorManager;
	}

	void Device::CreateInstance()
	{
	    if (_enableValidationLayers && CheckValidationLayerSupport() == false) 
	        throw runtime_error{ "validation layers requested, but not available!" };

	    VkApplicationInfo appInfo{};
	    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	    appInfo.pApplicationName = "Anggeum Engine";
	    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	    appInfo.pEngineName = "Anggeum Engine";
	    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	    appInfo.apiVersion = VK_API_VERSION_1_3;

	    VkInstanceCreateInfo createInfo{};
	    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	    createInfo.pApplicationInfo = &appInfo;
	    
	    auto extensions = GetRequiredExtensions();

	    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	    createInfo.ppEnabledExtensionNames = extensions.data();
	    
	    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	    if (_enableValidationLayers) 
	    {
	        createInfo.enabledLayerCount = static_cast<uint32_t>(_validationLayers.size());
	        createInfo.ppEnabledLayerNames = _validationLayers.data();

	        PopulateDebugMessengerCreateInfo(debugCreateInfo);
	        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
	    }
	    else
	    {
	        createInfo.enabledLayerCount = 0;
	        createInfo.pNext = nullptr;
	    }

	    if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS)
	        throw runtime_error("failed to create instance!");
	}

	bool Device::CheckValidationLayerSupport()
	{
	    uint32_t layerCount;
	    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	    vector<VkLayerProperties> availableLayers(layerCount);
	    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	    for (const char* layerName : _validationLayers) 
	    {
	        bool layerFound = false;

	        for (const auto& layerProperties : availableLayers) 
	        {
	            if (strcmp(layerName, layerProperties.layerName) == 0) 
	            {
	                layerFound = true;
	                break;
	            }
	        }

	        if (layerFound == false)
	            return false;
	    }

	    return true;
	}

	vector<const char*> Device::GetRequiredExtensions()
	{
	    uint32_t glfwExtensionCount = 0;
	    const char** glfwExtensions;

	    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	    vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

	    if (_enableValidationLayers)
	        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	    return extensions;
	}

	void Device::SetupDebugMessenger()
	{
	    if (_enableValidationLayers == false)
	        return;

	    VkDebugUtilsMessengerCreateInfoEXT createInfo;
	    PopulateDebugMessengerCreateInfo(createInfo);

		if (CreateDebugUtilsMessengerEXT(_instance, &createInfo, nullptr, &_debugMessenger) !=
			VK_SUCCESS)
			throw runtime_error("failed to set up debug messenger!");
	}

	void Device::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
	{
	    createInfo = {};
	    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	    createInfo.pfnUserCallback = DebugCallback;
	}

	void Device::PickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);

		if (deviceCount == 0)
			throw runtime_error("failed to find GPUs with Vulkan support!");

		vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

		for (const auto& device : devices)
		{
			if (IsDeviceSuitable(device))
			{
				_physicalDevice = device;

				CheckDescriptorIndexingSupport(device);

				break;
			}
		}

		if (_physicalDevice == VK_NULL_HANDLE)
			throw runtime_error("failed to find a suitable GPU!");
	}

	QueueFamilyIndices Device::FindQueueFamilies(VkPhysicalDevice device)
	{
	    QueueFamilyIndices indices;

	    uint32_t queueFamilyCount = 0;
	    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	    vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	    int i = 0;
	    for (const auto& queueFamily : queueFamilies) 
	    {
			if (indices.IsComplete())
				break;

			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				indices.GraphicsFamily = i;

	        if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
	            (queueFamily.queueFlags & ~VK_QUEUE_GRAPHICS_BIT))
	            indices.ComputeFamily = i;

			if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
				(queueFamily.queueFlags & ~VK_QUEUE_GRAPHICS_BIT) &&
	            (queueFamily.queueFlags & ~VK_QUEUE_COMPUTE_BIT))
				indices.TransferFamily = i;

	        VkBool32 presentSupport = false;
	        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);

	        if (presentSupport)
	            indices.PresentFamily = i;

	        i++;
	    }

	    return indices;
	}

	bool Device::IsDeviceSuitable(VkPhysicalDevice device)
	{
	    QueueFamilyIndices indices = FindQueueFamilies(device);

	    bool extensionsSupported = CheckDeviceExtensionSupport(device);

	    bool swapChainAdequate = false;
	    if (extensionsSupported) 
	    {
	        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
	        swapChainAdequate = 
	            !swapChainSupport.Formats.empty() && 
	            !swapChainSupport.PresentModes.empty();
	    }

	    VkPhysicalDeviceFeatures supportedFeatures;
	    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

	    return indices.IsComplete() && extensionsSupported && swapChainAdequate &&
	        supportedFeatures.samplerAnisotropy;
	}

	bool Device::CheckDeviceExtensionSupport(VkPhysicalDevice device)
	{
	    uint32_t extensionCount;
	    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	    vector<VkExtensionProperties> availableExtensions(extensionCount);
	    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

	    set<string> requiredExtensions(_deviceExtensions.begin(), _deviceExtensions.end());

	    for (const auto& extension : availableExtensions) 
	    {
	        requiredExtensions.erase(extension.extensionName);
	    }

	    return requiredExtensions.empty();
	}

	void Device::CreateLogicalDevice()
	{
	    // ============================================
	    // Step 2: Create logical device with requested features
	    // ============================================
	    VkPhysicalDeviceFeatures2 physicalFeatures2{};
	    physicalFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

	    VkPhysicalDeviceFeatures deviceFeatures{};
	    deviceFeatures.samplerAnisotropy = VK_TRUE;
	    deviceFeatures.sampleRateShading = VK_TRUE;
	    deviceFeatures.fillModeNonSolid = VK_TRUE;
	    physicalFeatures2.features = deviceFeatures;

		VkPhysicalDeviceTimelineSemaphoreFeatures timelineSempahoreFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
	    physicalFeatures2.pNext = &timelineSempahoreFeatures;

		// Enable descriptor indexing features if supported
		if (_supportsDescriptorIndexing)
		{
			_descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
			_descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
			_descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
			_descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;

			timelineSempahoreFeatures.pNext = &_descriptorIndexingFeatures;
		}

		vkGetPhysicalDeviceFeatures2(_physicalDevice, &physicalFeatures2);

	    QueueFamilyIndices indices = FindQueueFamilies(_physicalDevice);

	    vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	    set<uint32_t> uniqueQueueFamilies = {
	        indices.GraphicsFamily.value(),
	        indices.PresentFamily.value(),
	        indices.TransferFamily.value(),
	        indices.ComputeFamily.value() };

	    float queuePriority = 1.0f;
	    for (uint32_t queueFamily : uniqueQueueFamilies)
	    {
	        VkDeviceQueueCreateInfo queueCreateInfo{};
	        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	        queueCreateInfo.queueFamilyIndex = queueFamily;
	        queueCreateInfo.queueCount = 1;
	        queueCreateInfo.pQueuePriorities = &queuePriority;
	        queueCreateInfos.push_back(queueCreateInfo);
	    }

	    VkDeviceCreateInfo createInfo{};
	    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
	    createInfo.pQueueCreateInfos = queueCreateInfos.data();
	    createInfo.enabledExtensionCount = static_cast<uint32_t>(_deviceExtensions.size());
	    createInfo.ppEnabledExtensionNames = _deviceExtensions.data();
	    createInfo.pNext = &physicalFeatures2;

	    if (_enableValidationLayers)
	    {
	        createInfo.enabledLayerCount = static_cast<uint32_t>(_validationLayers.size());
	        createInfo.ppEnabledLayerNames = _validationLayers.data();
	    }
	    else
	        createInfo.enabledLayerCount = 0;

	    if (vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device) != VK_SUCCESS)
	    {
	        throw runtime_error("failed to create logical device!");
	    }

	    vkGetDeviceQueue(_device, indices.GraphicsFamily.value(), 0, &_graphicsQueue);
	    vkGetDeviceQueue(_device, indices.ComputeFamily.value(), 0, &_computeQueue);
	    vkGetDeviceQueue(_device, indices.PresentFamily.value(), 0, &_presentQueue);
	    vkGetDeviceQueue(_device, indices.TransferFamily.value(), 0, &_transferQueue);
	}

	SwapChainSupportDetails Device::QuerySwapChainSupport(VkPhysicalDevice device)
	{
	    SwapChainSupportDetails details{};
	    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &details.Capabilities);

	    uint32_t formatCount{};
	    vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);

	    if (formatCount != 0) 
	    {
	        details.Formats.resize(formatCount);
	        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, details.Formats.data());
	    }

	    uint32_t presentModeCount{};
	    vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, nullptr);

	    if (presentModeCount != 0) 
	    {
	        details.PresentModes.resize(presentModeCount);
	        vkGetPhysicalDeviceSurfacePresentModesKHR(
	            device,
	            _surface,
	            &presentModeCount,
	            details.PresentModes.data());
	    }

	    return details;
	}

	void Device::CheckDescriptorIndexingSupport(VkPhysicalDevice device)
	{
		_descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		_descriptorIndexingFeatures.pNext = nullptr;

		VkPhysicalDeviceFeatures2 deviceFeatures2{};
		deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		deviceFeatures2.pNext = &_descriptorIndexingFeatures;

		vkGetPhysicalDeviceFeatures2(device, &deviceFeatures2);

		// Check if all required features are supported
		_supportsDescriptorIndexing =
			_descriptorIndexingFeatures.descriptorBindingPartiallyBound &&
			_descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount &&
			_descriptorIndexingFeatures.runtimeDescriptorArray &&
			_descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending;

		if (_supportsDescriptorIndexing)
		{
			cout << "Descriptor Indexing (Bindless Textures) supported!" << endl;
		}
		else
		{
			cout << "Warning: Descriptor Indexing not fully supported. Bindless textures disabled." << endl;
		}
	}
}