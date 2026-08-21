#include "stdafx.h"
#include "Device.h"
#include "CommandPool.h"
#include "CommandBuffer.h"
#include "MemoryAllocator.h"
#include "Graphics/ResourceManager.h"

namespace Core
{
	struct DeviceFeatureChain
	{
		VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

		DeviceFeatureChain() = default;
		DeviceFeatureChain(const DeviceFeatureChain&) = delete;
		DeviceFeatureChain& operator=(const DeviceFeatureChain&) = delete;

		// Fills every supported feature bit for `device`.
		void Query(VkPhysicalDevice device)
		{
			features12.pNext = &features13;
			features2.pNext = &features12;
			vkGetPhysicalDeviceFeatures2(device, &features2);
		}

		bool HasRequiredFeatures() const
		{
			return features2.features.samplerAnisotropy &&
				features12.timelineSemaphore &&
				features13.dynamicRendering &&
				features13.synchronization2;
		}
	};

	Device::Device(Window& window)
		:_device(), _graphicsQueue(), _instance(), _surface(),
		_deviceExtensions{
			// Timeline semaphores and descriptor indexing are core since 1.2
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		}
	{
	    CreateInstance();

	    window.CreateSurface(_instance, &_surface);

		// Filled for the picked device during selection; CreateLogicalDevice
		// enables the same chain, so the features are queried exactly once.
		DeviceFeatureChain featureChain;
	    PickPhysicalDevice(featureChain);

		_supportsDrawIndirectCount = featureChain.features12.drawIndirectCount;

		_queueFamilyIndices = FindQueueFamilies(_physicalDevice);

		CreateLogicalDevice(featureChain);

		_debugUtils.Initialize(_instance, _device);

	    _graphicsCommandPool = new CommandPool(*this,
	        _queueFamilyIndices.GraphicsFamily.value());

	    _memoryAllocatorManager = new MemoryAllocatorManager(*this);

	    _resourceManager = new ResourceManager(*this);
	}

	Device::~Device()
	{
	    delete(_resourceManager);
	    delete(_memoryAllocatorManager);
	    delete(_graphicsCommandPool);

	    vkDestroyDevice(_device, nullptr);

	    _debugUtils.DestroyMessenger(_instance);

	    vkDestroySurfaceKHR(_instance, _surface, nullptr);
	    vkDestroyInstance(_instance, nullptr);
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

	    commandBuffer.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

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
	    if (_debugUtils.IsValidationLayerEnabled() &&
	        _debugUtils.CheckValidationLayerSupport() == false)
	        throw runtime_error{ "validation layers requested, but not available!" };

	    VkApplicationInfo appInfo{};
	    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	    appInfo.pApplicationName = "Anggeum Engine";
	    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	    appInfo.pEngineName = "Anggeum Engine";
	    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	    appInfo.apiVersion = VK_API_VERSION_1_4;

	    VkInstanceCreateInfo createInfo{};
	    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	    createInfo.pApplicationInfo = &appInfo;
	    
	    auto extensions = GetRequiredExtensions();

	    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	    createInfo.ppEnabledExtensionNames = extensions.data();
	    
	    const auto& validationLayers = _debugUtils.GetValidationLayers();
	    if (_debugUtils.IsValidationLayerEnabled())
	    {
	        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	        createInfo.ppEnabledLayerNames = validationLayers.data();
	    }
	    else
	        createInfo.enabledLayerCount = 0;

	    // Messenger covering instance creation/destruction. Belongs to debug utils,
	    // not the validation layers — with validation off it simply stays quiet.
	    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	    if (_debugUtils.IsDebugUtilsEnabled())
	    {
	        _debugUtils.PopulateMessengerCreateInfo(debugCreateInfo);
	        createInfo.pNext = &debugCreateInfo;
	    }
	    else
	        createInfo.pNext = nullptr;

	    if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS)
	        throw runtime_error("failed to create instance!");
	}

	vector<const char*> Device::GetRequiredExtensions()
	{
	    uint32_t glfwExtensionCount = 0;
	    const char** glfwExtensions;

	    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	    vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

	    if (_debugUtils.IsDebugUtilsEnabled())
	        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	    return extensions;
	}

	void Device::PickPhysicalDevice(DeviceFeatureChain& outFeatureChain)
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);

		if (deviceCount == 0)
			throw runtime_error("failed to find GPUs with Vulkan support!");

		vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

		for (const auto& device : devices)
		{
			if (IsDeviceSuitable(device, outFeatureChain))
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
			{
				indices.GraphicsFamily = i;
				indices.TransferFamily = i;
			}

			if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
				!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
				indices.ComputeFamily = i;

			//if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
			//	!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			//	!(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT))
			//	indices.TransferFamily = i;

	        VkBool32 presentSupport = false;
	        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);

	        if (presentSupport)
	        {
	            const bool isGraphicsFamily = (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
	            const bool haveGraphicsPresent = indices.PresentFamily.has_value() &&
	                indices.GraphicsFamily.has_value() &&
	                indices.PresentFamily.value() == indices.GraphicsFamily.value();

	            if (isGraphicsFamily || haveGraphicsPresent == false)
	                indices.PresentFamily = i;
	        }

			i++;
		}

		printf("[QUEUE] GraphicsFamily=%d ComputeFamily=%d PresentFamily=%d TransferFamily=%d\n",
			indices.GraphicsFamily.has_value() ? (int)indices.GraphicsFamily.value() : -1,
			indices.ComputeFamily.has_value() ? (int)indices.ComputeFamily.value() : -1,
			indices.PresentFamily.has_value() ? (int)indices.PresentFamily.value() : -1,
			indices.TransferFamily.has_value() ? (int)indices.TransferFamily.value() : -1);

		return indices;
	}

	bool Device::IsDeviceSuitable(VkPhysicalDevice device, DeviceFeatureChain& outFeatureChain)
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

	    VkPhysicalDeviceProperties properties;
	    vkGetPhysicalDeviceProperties(device, &properties);

	    outFeatureChain.Query(device);

	    return indices.IsComplete() && extensionsSupported && swapChainAdequate &&
	        properties.apiVersion >= VK_API_VERSION_1_4 &&
	        outFeatureChain.HasRequiredFeatures();
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

	void Device::CreateLogicalDevice(const DeviceFeatureChain& featureChain)
	{
		assert(featureChain.HasRequiredFeatures());

	    vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	    set<uint32_t> uniqueQueueFamilies = {
			_queueFamilyIndices.GraphicsFamily.value(),
			_queueFamilyIndices.PresentFamily.value(),
			_queueFamilyIndices.TransferFamily.value(),
			_queueFamilyIndices.ComputeFamily.value() };

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
	    createInfo.pNext = &featureChain.features2;

	    const auto& validationLayers = _debugUtils.GetValidationLayers();
	    if (_debugUtils.IsValidationLayerEnabled())
	    {
	        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	        createInfo.ppEnabledLayerNames = validationLayers.data();
	    }
	    else
	        createInfo.enabledLayerCount = 0;

	    if (vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device) != VK_SUCCESS)
	    {
	        throw runtime_error("failed to create logical device!");
	    }

	    vkGetDeviceQueue(_device, _queueFamilyIndices.GraphicsFamily.value(), 0, &_graphicsQueue);
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