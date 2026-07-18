#include "stdafx.h"
#include "DebugUtils.h"

namespace
{
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
}

namespace Core
{
	bool DebugUtils::GetDebugFlag(const char* envName)
	{
#ifdef NDEBUG
		bool enabled = false;
#else
		bool enabled = true;
#endif

		if (const char* env = getenv(envName))
		{
			if (strcmp(env, "0") == 0)
				enabled = false;
			else if (strcmp(env, "1") == 0)
				enabled = true;
		}

		return enabled;
	}

	bool DebugUtils::CheckValidationLayerSupport() const
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

	void DebugUtils::PopulateMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const
	{
		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = DebugCallback;
	}

	void DebugUtils::Initialize(VkInstance instance, VkDevice device)
	{
		_device = device;

		LoadFunctions(instance);
		CreateMessenger(instance);
	}

	void DebugUtils::CreateMessenger(VkInstance instance)
	{
		if (_enableDebugUtils == false)
			return;

		VkDebugUtilsMessengerCreateInfoEXT createInfo;
		PopulateMessengerCreateInfo(createInfo);

		if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &_messenger) != VK_SUCCESS)
			throw runtime_error("failed to set up debug messenger!");
	}

	void DebugUtils::DestroyMessenger(VkInstance instance)
	{
		if (_messenger == VK_NULL_HANDLE)
			return;

		DestroyDebugUtilsMessengerEXT(instance, _messenger, nullptr);
		_messenger = VK_NULL_HANDLE;
	}

	void DebugUtils::LoadFunctions(VkInstance instance)
	{
		// Without the extension enabled these functions must not be called, so leave
		// the pointers null — the debug marker/object name calls no-op on null.
		if (_enableDebugUtils == false)
			return;

		// Load from instance, not device
		_vkCmdBeginDebugUtilsLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT");
		_vkCmdEndDebugUtilsLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT");
		_vkCmdInsertDebugUtilsLabel = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT");
		_vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");

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

	void DebugUtils::BeginLabel(VkCommandBuffer commandBuffer, const char* labelName, const float color[4]) const
	{
		if (_vkCmdBeginDebugUtilsLabel == nullptr)
			return;

		VkDebugUtilsLabelEXT labelInfo{};
		labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		labelInfo.pLabelName = labelName;
		labelInfo.color[0] = color[0];
		labelInfo.color[1] = color[1];
		labelInfo.color[2] = color[2];
		labelInfo.color[3] = color[3];

		_vkCmdBeginDebugUtilsLabel(commandBuffer, &labelInfo);
	}

	void DebugUtils::EndLabel(VkCommandBuffer commandBuffer) const
	{
		if (_vkCmdEndDebugUtilsLabel == nullptr)
			return;

		_vkCmdEndDebugUtilsLabel(commandBuffer);
	}

	void DebugUtils::InsertLabel(VkCommandBuffer commandBuffer, const char* labelName, const float color[4]) const
	{
		if (_vkCmdInsertDebugUtilsLabel == nullptr)
			return;

		VkDebugUtilsLabelEXT labelInfo{};
		labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		labelInfo.pLabelName = labelName;
		labelInfo.color[0] = color[0];
		labelInfo.color[1] = color[1];
		labelInfo.color[2] = color[2];
		labelInfo.color[3] = color[3];

		_vkCmdInsertDebugUtilsLabel(commandBuffer, &labelInfo);
	}

	void DebugUtils::SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name) const
	{
		if (_vkSetDebugUtilsObjectName == nullptr || _device == VK_NULL_HANDLE)
			return;

		if (name == nullptr || objectHandle == 0)
			return;

		VkDebugUtilsObjectNameInfoEXT nameInfo{};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = objectType;
		nameInfo.objectHandle = objectHandle;
		nameInfo.pObjectName = name;

		_vkSetDebugUtilsObjectName(_device, &nameInfo);
	}

	string DebugUtils::FormatPassLabels(const VkDebugUtilsMessengerCallbackDataEXT* callbackData)
	{
		// Command buffer labels are the interesting ones — one is opened per
		// RendererPass. Queue labels only serve as a fallback for messages raised
		// outside any command buffer.
		const VkDebugUtilsLabelEXT* labels = callbackData->pCmdBufLabels;
		uint32_t labelCount = callbackData->cmdBufLabelCount;

		if (labelCount == 0)
		{
			labels = callbackData->pQueueLabels;
			labelCount = callbackData->queueLabelCount;
		}

		if (labels == nullptr || labelCount == 0)
			return {};

		// The layer hands the labels back most recent first, so walk backwards to
		// read outer pass -> inner marker.
		ostringstream stream;
		bool isFirst = true;
		for (uint32_t i = labelCount; i > 0; i--)
		{
			const char* labelName = labels[i - 1].pLabelName;
			if (labelName == nullptr)
				continue;

			if (isFirst == false)
				stream << " / ";

			stream << labelName;
			isFirst = false;
		}

		return stream.str();
	}

	string DebugUtils::FormatObjects(const VkDebugUtilsMessengerCallbackDataEXT* callbackData)
	{
		if (callbackData->pObjects == nullptr || callbackData->objectCount == 0)
			return {};

		ostringstream stream;
		for (uint32_t i = 0; i < callbackData->objectCount; i++)
		{
			const auto& object = callbackData->pObjects[i];

			if (i != 0)
				stream << ", ";

			stream << ToString(object.objectType) << " ";

			// Unnamed handles fall back to the raw value — name the resource at
			// creation time with SetObjectName to get something readable here.
			if (object.pObjectName != nullptr)
				stream << "\"" << object.pObjectName << "\"";
			else
				stream << "0x" << hex << object.objectHandle << dec;
		}

		return stream.str();
	}

	const char* DebugUtils::ToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
	{
		switch (severity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "ERROR";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "INFO";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
		default: return "UNKNOWN";
		}
	}

	const char* DebugUtils::ToString(VkObjectType objectType)
	{
		switch (objectType)
		{
		case VK_OBJECT_TYPE_INSTANCE: return "Instance";
		case VK_OBJECT_TYPE_PHYSICAL_DEVICE: return "PhysicalDevice";
		case VK_OBJECT_TYPE_DEVICE: return "Device";
		case VK_OBJECT_TYPE_QUEUE: return "Queue";
		case VK_OBJECT_TYPE_SEMAPHORE: return "Semaphore";
		case VK_OBJECT_TYPE_COMMAND_BUFFER: return "CommandBuffer";
		case VK_OBJECT_TYPE_FENCE: return "Fence";
		case VK_OBJECT_TYPE_DEVICE_MEMORY: return "DeviceMemory";
		case VK_OBJECT_TYPE_BUFFER: return "Buffer";
		case VK_OBJECT_TYPE_IMAGE: return "Image";
		case VK_OBJECT_TYPE_EVENT: return "Event";
		case VK_OBJECT_TYPE_QUERY_POOL: return "QueryPool";
		case VK_OBJECT_TYPE_BUFFER_VIEW: return "BufferView";
		case VK_OBJECT_TYPE_IMAGE_VIEW: return "ImageView";
		case VK_OBJECT_TYPE_SHADER_MODULE: return "ShaderModule";
		case VK_OBJECT_TYPE_PIPELINE_CACHE: return "PipelineCache";
		case VK_OBJECT_TYPE_PIPELINE_LAYOUT: return "PipelineLayout";
		case VK_OBJECT_TYPE_RENDER_PASS: return "RenderPass";
		case VK_OBJECT_TYPE_PIPELINE: return "Pipeline";
		case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: return "DescriptorSetLayout";
		case VK_OBJECT_TYPE_SAMPLER: return "Sampler";
		case VK_OBJECT_TYPE_DESCRIPTOR_POOL: return "DescriptorPool";
		case VK_OBJECT_TYPE_DESCRIPTOR_SET: return "DescriptorSet";
		case VK_OBJECT_TYPE_FRAMEBUFFER: return "Framebuffer";
		case VK_OBJECT_TYPE_COMMAND_POOL: return "CommandPool";
		case VK_OBJECT_TYPE_SURFACE_KHR: return "Surface";
		case VK_OBJECT_TYPE_SWAPCHAIN_KHR: return "SwapChain";
		default: return "Object";
		}
	}

	VkBool32 DebugUtils::DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData)
	{
		ostringstream stream;
		stream << "validation layer [" << ToString(messageSeverity) << "]";

		const string passLabels = FormatPassLabels(pCallbackData);
		if (passLabels.empty() == false)
			stream << " [Pass: " << passLabels << "]";

		const string objects = FormatObjects(pCallbackData);
		if (objects.empty() == false)
			stream << " [Resource: " << objects << "]";

		if (pCallbackData->pMessageIdName != nullptr)
			stream << "\n  " << pCallbackData->pMessageIdName;

		if (pCallbackData->pMessage != nullptr)
			stream << "\n  " << pCallbackData->pMessage;

		const bool isProblem = (messageSeverity &
			(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) != 0;

		if (isProblem)
			cerr << stream.str() << endl;
		else
			cout << stream.str() << endl;

		return VK_FALSE;
	}
}
