#pragma once

namespace Core
{
	class SwapChain
	{
	public:
		SwapChain(Device& device);
		~SwapChain();

		VkSwapchainKHR GetSwapChain() const { return _swapChain; }
		void RecreateSwapChain();

		void GetViewportAndScissor(VkViewport& viewport, VkRect2D& scissor);
		VkExtent2D GetSwapChainExtent() { return _swapChainExtent; }

		size_t GetSwapChainCount() const { return _swapChainImages.size(); }
		VkImageView GetImageView(size_t swapChainIndex) const;
		VkImage GetImage(size_t swapChainIndex) const { return _swapChainImages[swapChainIndex]; }

		// Present-wait semaphore for image i, signaled by the frame's last
		// graphics submit and waited by Present.
		VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const
		{
			return _renderFinishedPerImage[imageIndex];
		}
		VkFormat GetImageFormat() const 
		{
			return _swapChainImageFormat;
		}
	private:
		void CreateSwapChain();
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const vector<VkPresentModeKHR>& availablePresentModes);

		void CreateImageViews();
		void CreateRenderFinishedSemaphores();
		void CleanupSwapChain();
	private:
		Device& _device;

		VkSwapchainKHR _swapChain;
		vector<VkImage> _swapChainImages;
		VkFormat _swapChainImageFormat;
		VkExtent2D _swapChainExtent;
		vector<VkImageView> _swapChainImageViews;
		vector<VkSemaphore> _renderFinishedPerImage;
	};
}

