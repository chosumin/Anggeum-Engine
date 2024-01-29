#pragma once

namespace Core
{
	class CommandBuffer;
	class Swapchain;
	class RenderContext
	{
	public:
		// The format to use for the RenderTargets if a swapchain isn't created
		static VkFormat DEFAULT_VK_FORMAT;

		RenderContext(Device& device,
			VkSurfaceKHR surface,
			const Window& window,
			VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR,
			const std::vector<VkPresentModeKHR>& presentModePriorityList = 
			{ VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR },
			const std::vector<VkSurfaceFormatKHR>& surfaceFormatPriorityList = 
			{ {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
			{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} });

		RenderContext(const RenderContext&) = delete;

		RenderContext(RenderContext&&) = delete;

		virtual ~RenderContext() = default;

		RenderContext& operator=(const RenderContext&) = delete;

		RenderContext& operator=(RenderContext&&) = delete;

		void Prepare(size_t threadCount = 1, 
			RenderTarget::CreateFunc createRenderTarget = 
			RenderTarget::DEFAULT_CREATE_FUNC);

		void UpdateSwapchain(const VkExtent2D& extent);
		void UpdateSwapchain(const uint32_t imageCount);
		void UpdateSwapchain(const set<VkImageUsageFlagBits>& imageUsageFlags);
		void UpdateSwapchain(const VkExtent2D& extent, 
			const VkSurfaceTransformFlagBitsKHR transform);

		bool HasSwapchain();
		void Recreate();
		void RecreateSwapchain();

		CommandBuffer& Begin(CommandBuffer::ResetMode resetMode = 
			CommandBuffer::ResetMode::ResetPool);

		void Submit(CommandBuffer& commandBuffer);
		void Submit(const vector<CommandBuffer*>& commandBuffers);

		void BeginFrame();
		
		VkSemaphore Submit(const Queue& queue, 
			const std::vector<CommandBuffer*>& command_buffers, 
			VkSemaphore wait_semaphore, 
			VkPipelineStageFlags wait_pipeline_stage);
		void Submit(const Queue& queue, const std::vector<CommandBuffer*>& command_buffers);

		virtual void WaitFrame();

		void EndFrame(VkSemaphore semaphore);

		RenderFrame& GetActiveFrame();
		uint32_t GetActiveFrameIndex();
		RenderFrame& GetLastRenderedFrame();

		VkSemaphore RequestSemaphore();
		VkSemaphore RequestSemaphoreWithOwnership();
		void ReleaseOwnedSemaphore(VkSemaphore semaphore);

		Device& GetDevice();
		VkFormat GetFormat() const;
		Swapchain const& GetSwapchain() const;
		VkExtent2D const& GetSurfaceExtent() const;
		uint32_t GetActiveFrameIndex() const;

		vector<unique_ptr<RenderFrame>>& GetRenderFrames();

		virtual bool HandleSurfaceChanges(bool forceUpdate = false);
		VkSemaphore ConsumeAcquiredSemaphore();
	protected:
		VkExtent2D _surfaceExtent;
	private:
		Device& _device;
		const Window& _window;

		const Queue& _queue;
		unique_ptr<Swapchain> _swapchain;
		SwapchainProperties _swapchainProperties;

		vector<unique_ptr<RenderFrame>> _frames;
		VkSemaphore _acquiredSemaphore;
		bool _prepared{ false };
		uint32_t _activeFrameIndex{ 0 };
		bool _frameActive{ false };

		RenderTarget::CreateFunc _createRenderTarget = 
			RenderTarget::DEFAULT_CREATE_FUNC;

		VkSurfaceTransformFlagBitsKHR _preTransform{ VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR };

		size_t _threadCount{ 1 };
	};
}
