#pragma once

namespace Core
{
	class SwapChain
	{
	public:
		SwapChain();
		~SwapChain();

		//VkRenderPass GetRenderPass() const { return _renderPass; }

		VkSwapchainKHR GetSwapChain() const { return _swapChain; }
		void RecreateSwapChain();

		//VkRenderPassBeginInfo CreateRenderPassBeginInfo(uint32_t imageIndex);
		void GetViewportAndScissor(VkViewport& viewport, VkRect2D& scissor);
		VkExtent2D GetSwapChainExtent() { return _swapChainExtent; }

		size_t GetSwapChainCount() const { return _swapChainImages.size(); }
		VkImageView GetImageView(size_t swapChainIndex) const;
		VkFormat GetImageFormat() const 
		{
			return _swapChainImageFormat;
		}
	private:
		void CreateSwapChain();
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const vector<VkPresentModeKHR>& availablePresentModes);

		//void CreateDepthResources();
		//void CreateFramebuffers();
		//void CreateColorResources();

		void CreateImageViews();
		void CleanupSwapChain();
	private:
		VkSwapchainKHR _swapChain;
		vector<VkImage> _swapChainImages;
		VkFormat _swapChainImageFormat;
		VkExtent2D _swapChainExtent;
		vector<VkImageView> _swapChainImageViews;
		//vector<VkFramebuffer> _swapChainFramebuffers;

		/*VkImage _depthImage;
		VkDeviceMemory _depthImageMemory;
		VkImageView _depthImageView;

		VkImage _colorImage;
		VkDeviceMemory _colorImageMemory;
		VkImageView _colorImageView;*/

		//array<VkClearValue, 2> _clearValues{};
	};
}

