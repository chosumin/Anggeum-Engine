#include "stdafx.h"
#include "Device.h"

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

void Core::Device::Initialize(Window& window)
{
    CreateInstance();
    SetupDebugMessenger();
    window.CreateSurface(_instance, &_surface);
    PickPhysicalDevice();
    CreateLogicalDevice();
}

void Core::Device::Delete()
{
    vkDestroyDevice(_device, nullptr);

    if (_enableValidationLayers)
        DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);

    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    vkDestroyInstance(_instance, nullptr);
}

uint32_t Core::Device::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
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

    throw std::runtime_error("failed to find suitable memory type!");
}

Core::Device::Device()
    :_device(), _debugMessenger(), _graphicsQueue(), _presentQueue(), _instance(), _surface()
{
}

Core::Device::~Device()
{
}

void Core::Device::CreateInstance()
{
    if (_enableValidationLayers && CheckValidationLayerSupport() == false) 
        throw std::runtime_error{ "validation layers requested, but not available!" };

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

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

    //_instance = make_unique<VkInstance>();

    if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS)
        throw runtime_error("failed to create instance!");
}

bool Core::Device::CheckValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
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

vector<const char*> Core::Device::GetRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;

    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (_enableValidationLayers)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

void Core::Device::SetupDebugMessenger()
{
    if (_enableValidationLayers == false)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    PopulateDebugMessengerCreateInfo(createInfo);

	if (CreateDebugUtilsMessengerEXT(_instance, &createInfo, nullptr, &_debugMessenger) !=
		VK_SUCCESS)
		throw runtime_error("failed to set up debug messenger!");
}

void Core::Device::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;
}

void Core::Device::PickPhysicalDevice()
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);

	if (deviceCount == 0)
		throw runtime_error("failed to find GPUs with Vulkan support!");

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

	for (const auto& device : devices)
	{
		if (IsDeviceSuitable(device))
		{
			_physicalDevice = device;
			break;
		}
	}

	if (_physicalDevice == VK_NULL_HANDLE)
	{
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

int Core::Device::RateDeviceSuitability(VkPhysicalDevice device)
{
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    int score = 0;

    // Discrete GPUs have a significant performance advantage
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;

    // Maximum possible size of textures affects graphics quality
    score += deviceProperties.limits.maxImageDimension2D;

    // Application can't function without geometry shaders
    if (!deviceFeatures.geometryShader)
        return 0;

    return score;
}

Core::QueueFamilyIndices Core::Device::FindQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) 
    {
        if (indices.IsComplete())
            break;

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.GraphicsFamily = i;

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);

        if (presentSupport)
            indices.PresentFamily = i;

        i++;
    }

    return indices;
}

bool Core::Device::IsDeviceSuitable(VkPhysicalDevice device)
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

bool Core::Device::CheckDeviceExtensionSupport(VkPhysicalDevice device)
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

void Core::Device::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamilies(_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = 
        { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

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

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = _deviceExtensions.data();

    if (_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(_validationLayers.size());
        createInfo.ppEnabledLayerNames = _validationLayers.data();
    }
    else
        createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(_device, indices.GraphicsFamily.value(), 0, &_graphicsQueue);
    vkGetDeviceQueue(_device, indices.PresentFamily.value(), 0, &_presentQueue);
}

Core::SwapChainSupportDetails Core::Device::QuerySwapChainSupport(VkPhysicalDevice device)
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