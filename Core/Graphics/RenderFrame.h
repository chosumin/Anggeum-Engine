//#pragma once
//
//namespace Core
//{
//	class RenderTarget;
//	class CommandBuffer;
//	class RenderFrame
//	{
//	public:
//		RenderFrame(Device& device, RenderTarget* renderTarget, size_t threadCount = 1);
//
//		RenderFrame(const RenderFrame&) = delete;
//		RenderFrame(RenderFrame&&) = delete;
//
//		RenderFrame& operator=(const RenderFrame&) = delete;
//		RenderFrame& operator=(RenderFrame&&) = delete;
//
//		void Reset();
//
//		//const FencePool& get_fence_pool() const;
//		//VkFence request_fence();
//		//const SemaphorePool& get_semaphore_pool() const;
//		/*VkSemaphore request_semaphore();
//		VkSemaphore request_semaphore_with_ownership();
//		void        release_owned_semaphore(VkSemaphore semaphore);*/
//
//		void UpdateRenderTarget(RenderTarget* renderTarget);
//		RenderTarget& GetRenderTarget();
//		const RenderTarget& GetRenderTargetConst() const;
//
//		/*CommandBuffer& RequestCommandBuffer(const Queue& queue,
//			CommandBuffer::ResetMode reset_mode = CommandBuffer::ResetMode::ResetPool,
//			VkCommandBufferLevel     level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
//			size_t                   thread_index = 0);
//
//		VkDescriptorSet RequestDescriptorSet(const DescriptorSetLayout& descriptor_set_layout,
//			const BindingMap<VkDescriptorBufferInfo>& buffer_infos,
//			const BindingMap<VkDescriptorImageInfo>& image_infos,
//			bool                                      update_after_bind,
//			size_t                                    thread_index = 0);*/
//
//		void ClearDescriptors();
//
//		void SetDescriptorManagementStrategy(DescriptorManagementStrategy newStrategy);
//
//		void UpdateDescriptorSets(size_t threadIndex = 0);
//	private:
//		static vector<uint32_t> CollectBindingsToUpdate(
//			const DescriptorSetLayout& descriptorSetLayout, 
//			const BindingMap<VkDescriptorBufferInfo>& bufferInfos, 
//			const BindingMap<VkDescriptorImageInfo>& imageInfos);
//	private:
//		vector<unique_ptr<CommandPool>>& GetCommandPools(
//			const Queue& queue, CommandBuffer::ResetMode resetMode);
//	private:
//		map<uint32_t, vector<unique_ptr<CommandPool>>> _commandPools;
//		vector<unique_ptr<unordered_map<size_t, DescriptorPool>>> _descriptorPools;
//		vector<unique_ptr<unordered_map<size_t, DescriptorSet>>> _descriptorSets;
//
//		FencePool _fencePool;
//		SemaphorePool _semaphorePool;
//
//		size_t _threadCount;
//		RenderTarget* _swapchainRenderTarget;
//
//		DescriptorManagementStrategy _descriptorManagementStrategy
//			{ DescriptorManagementStrategy::StoreInCache };
//	};
//}