#include "stdafx.h"
#include "RenderFrame.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/DescriptorPool.h"

namespace Core
{
	RenderFrame::RenderFrame(Device& device)
		: _device(device)
	{
		CreateSyncObjects();
		CreateDescriptorPool();
	}
	
	RenderFrame::~RenderFrame()
	{
		// Clean up descriptor pool
		_descriptorPool.reset();
		
		// Clean up sync objects
		if (_imageAvailableSemaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(_device.GetDevice(), _imageAvailableSemaphore, nullptr);
		
		if (_renderFinishedSemaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(_device.GetDevice(), _renderFinishedSemaphore, nullptr);
	}
	
	void RenderFrame::Reset()
	{
		// Reset descriptor pool (release all descriptor sets)
		_descriptorPool->Reset();
		
		// Reset command buffers (managed by RenderContext)
		if (_commandBuffer)
			_commandBuffer->ResetCommandBuffer();
		
		if (_computeCommandBuffer)
			_computeCommandBuffer->ResetCommandBuffer();
	}
	
	void RenderFrame::CreateSyncObjects()
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		
		if (vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_imageAvailableSemaphore) != VK_SUCCESS ||
		    vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_renderFinishedSemaphore) != VK_SUCCESS)
		{
			throw runtime_error("Failed to create synchronization objects for a frame!");
		}
	}
	
	void RenderFrame::CreateDescriptorPool()
	{
		// Create per-frame descriptor pool
		_descriptorPool = make_unique<DescriptorPool>(_device);
		
		// Configure pool sizes (amount needed per frame)
		vector<VkDescriptorPoolSize> poolSizes = {
			// Uniform buffers
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
			// Combined image samplers (textures)
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500 },
			// Storage buffers
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 }
		};
		
		_descriptorPool->CreatePool(poolSizes, 100); // Maximum 100 descriptor sets
	}
}