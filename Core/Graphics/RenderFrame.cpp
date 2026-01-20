#include "stdafx.h"
#include "RenderFrame.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Graphics/Vulkans/UniformBuffer.h"
#include "Graphics/Vulkans/TextureBuffer.h"
#include "Graphics/Vulkans/StorageBuffer.h"

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
		CleanupBuffers();

		if (_imageAvailableSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(_device.GetDevice(), _imageAvailableSemaphore, nullptr);
		}
		if (_renderFinishedSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(_device.GetDevice(), _renderFinishedSemaphore, nullptr);
		}
	}

	void RenderFrame::Reset()
	{
		// Clean up buffers from previous frame
		//CleanupBuffers();

		// Reset descriptor pool
		if (_descriptorPool)
		{
			_descriptorPool->Reset();
		}
	}

	UniformBuffer* RenderFrame::CreateUniformBuffer(VkDeviceSize size)
	{
		auto buffer = new UniformBuffer(_device, size);
		_uniformBuffers.push_back(buffer);
		return buffer;
	}

	TextureBuffer* RenderFrame::CreateTextureBuffer()
	{
		auto buffer = new TextureBuffer();
		_textureBuffers.push_back(buffer);
		return buffer;
	}

	StorageBuffer* RenderFrame::CreateStorageBuffer()
	{
		auto buffer = new StorageBuffer();
		_storageBuffers.push_back(buffer);
		return buffer;
	}

	void RenderFrame::CleanupBuffers()
	{
		// Delete all uniform buffers
		for (auto buffer : _uniformBuffers)
		{
			delete buffer;
		}
		_uniformBuffers.clear();

		// Delete all texture buffers
		for (auto buffer : _textureBuffers)
		{
			delete buffer;
		}
		_textureBuffers.clear();

		// Delete all storage buffers
		for (auto buffer : _storageBuffers)
		{
			delete buffer;
		}
		_storageBuffers.clear();
	}

	void RenderFrame::CreateSyncObjects()
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		if (vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_imageAvailableSemaphore) != VK_SUCCESS ||
			vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_renderFinishedSemaphore) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create semaphores for a frame!");
		}
	}

	void RenderFrame::CreateDescriptorPool()
	{
		_descriptorPool = make_unique<DescriptorPool>(_device);
	}
}