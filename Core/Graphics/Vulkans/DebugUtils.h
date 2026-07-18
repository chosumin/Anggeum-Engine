#pragma once

namespace Core
{
	// Owns everything VK_EXT_debug_utils: the messenger that surfaces validation
	// layer messages, the command buffer label / object name entry points, and the
	// environment toggles that gate them.
	class DebugUtils
	{
	public:
		void Initialize(VkInstance instance, VkDevice device);

		bool IsValidationLayerEnabled() const { return _enableValidationLayers; }
		bool IsDebugUtilsEnabled() const { return _enableDebugUtils; }

		void PopulateMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const;

		const vector<const char*>& GetValidationLayers() const { return _validationLayers; }
		bool CheckValidationLayerSupport() const;

		void DestroyMessenger(VkInstance instance);

		void BeginLabel(VkCommandBuffer commandBuffer, const char* labelName, const float color[4]) const;
		void EndLabel(VkCommandBuffer commandBuffer) const;
		void InsertLabel(VkCommandBuffer commandBuffer, const char* labelName, const float color[4]) const;

		// Attaches a human readable name to a Vulkan handle. The name is what the
		// validation callback prints instead of a raw handle, so name anything that
		// can plausibly show up in an error.
		void SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name) const;
	private:
		void LoadFunctions(VkInstance instance);
		void CreateMessenger(VkInstance instance);
		
		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData);

		// The debug labels active on the command buffer when the message fired.
		// ForwardRenderPipeline opens one per RendererPass, so this reads back as
		// the pass name followed by any finer grained markers inside it.
		static string FormatPassLabels(const VkDebugUtilsMessengerCallbackDataEXT* callbackData);

		// The handles the layer blamed, by name when they have one.
		static string FormatObjects(const VkDebugUtilsMessengerCallbackDataEXT* callbackData);

		static const char* ToString(VkObjectType objectType);
		static const char* ToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity);

		// Reads a 0/1 override from the named environment variable, defaulting to on
		// in Debug builds and off in Release.
		static bool GetDebugFlag(const char* envName);

	private:
		VkDevice _device = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT _messenger = VK_NULL_HANDLE;

		PFN_vkCmdBeginDebugUtilsLabelEXT _vkCmdBeginDebugUtilsLabel = nullptr;
		PFN_vkCmdEndDebugUtilsLabelEXT _vkCmdEndDebugUtilsLabel = nullptr;
		PFN_vkCmdInsertDebugUtilsLabelEXT _vkCmdInsertDebugUtilsLabel = nullptr;
		PFN_vkSetDebugUtilsObjectNameEXT _vkSetDebugUtilsObjectName = nullptr;

		const vector<const char*> _validationLayers =
		{
			"VK_LAYER_KHRONOS_validation"
		};

		// VK_LAYER_KHRONOS_validation. Set DEBUG_VALIDATION=0 to turn this off when
		// running under a tool that injects its own Vulkan layers.
		const bool _enableValidationLayers = GetDebugFlag("DEBUG_VALIDATION");

		// VK_EXT_debug_utils: pass labels and object names for graphics debuggers,
		// plus the messenger that surfaces layer messages.
		const bool _enableDebugUtils = GetDebugFlag("DEBUG_UTILS");
	};
}
