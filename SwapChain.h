#pragma once

namespace Core
{
	class SwapChain
	{
	public:
		SwapChain();
		~SwapChain();

		VkRenderPass GetRenderPass() { return _renderPass; }

		VkSwapchainKHR GetSwapChain() { return _swapChain; }
		void RecreateSwapChain();

		VkRenderPassBeginInfo CreateRenderPassBeginInfo(uint32_t imageIndex);
		void GetViewportAndScissor(VkViewport& viewport, VkRect2D& scissor);
		VkExtent2D GetSwapChainExtent() { return _swapChainExtent; }

		void CreateDepthResources();
		void CreateFramebuffers();
	private:
		void CreateSwapChain();
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const vector<VkPresentModeKHR>& availablePresentModes);
		void CreateImageViews();
		void CreateRenderPass();
		void CleanupSwapChain();
		VkFormat FindDepthFormat();
	private:
		VkSwapchainKHR _swapChain;
		vector<VkImage> _swapChainImages;
		VkFormat _swapChainImageFormat;
		VkExtent2D _swapChainExtent;
		vector<VkImageView> _swapChainImageViews;
		vector<VkFramebuffer> _swapChainFramebuffers;
		VkRenderPass _renderPass;

		VkImage _depthImage;
		VkDeviceMemory _depthImageMemory;
		VkImageView _depthImageView;
	};
}

