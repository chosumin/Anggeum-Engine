#include "stdafx.h"
#include "RenderFrame.h"
#include "Material.h"
#include "Vulkans/UniformBuffer.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/Shader.h"
#include "Vulkans/Framebuffer.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "Vulkans/CommandBuffer.h"
#include "ResourceCache.h"
#include "TransferJob.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"

using namespace Core;

RenderFrame::RenderFrame(Device& device, BindlessTextureManager* bindlessManager)
	: _device(device)
	, _bindlessTextureManager(bindlessManager)
{
	CreateSyncObjects();
	CreateDescriptorPool();

	_defaultSampler = device.GetResourceCache().RequestSampler(DEFAULT_SAMPLER);
	_renderExecutor = make_unique<RenderExecutor>(device, *this);
}

RenderFrame::~RenderFrame()
{
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
	// Reset descriptor pool
	if (_descriptorPool)
	{
		_descriptorPool->Reset();
	}

	// Cleanup builder-created resources
	for (auto& resources : _builderResources)
	{
		resources.CleanupBuffers();
	}
	_builderResources.clear();

	// Reset culler usage tracking for this frame
	_renderExecutor->ResetFrame();

	_currentDepth = nullptr;
	_currentNormal = nullptr;
}

DescriptorSetResources* RenderFrame::GetBindlessResources()
{
	if (!HasBindlessSupport())
		return nullptr;

	_bindlessResources.descriptorSet = _bindlessTextureManager->GetDescriptorSet();
	_bindlessResources.setIndex = static_cast<uint32_t>(DescriptorSetType::Bindless);
	return &_bindlessResources;
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
	
	// Define pool sizes for different descriptor types
	vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 200 },
		//{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		//{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		//{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		//{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		//{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000
	};
	
	// Create pool with sufficient descriptor sets
	uint32_t maxSets = 1000;
	_descriptorPool->CreatePool(poolSizes, maxSets);
}

shared_ptr<Texture> Core::RenderFrame::GetRenderTarget(const string& name) const
{
    auto it = _renderTargets.find(name);
    if (it != _renderTargets.end())
        return it->second;
    return nullptr;
}

void Core::RenderFrame::SetPreviousDepthBuffer(shared_ptr<Texture> depth)
{
    _previousDepthBuffer = depth;
}

shared_ptr<Texture> Core::RenderFrame::GetOrCreateRenderTarget(const string& name,
    const RenderTargetDesc& desc)
{
    auto it = _renderTargets.find(name);
    if (it != _renderTargets.end())
        return it->second;

    return CreateRenderTarget(name, desc);
}

shared_ptr<Texture> Core::RenderFrame::CreateRenderTarget(const string& name,
    const RenderTargetDesc& desc)
{
    VkFormat format = desc.format;
    
	// Depth format fallback if undefined and aspect includes depth
    if (format == VK_FORMAT_UNDEFINED && desc.aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
    {
        format = _device.FindSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { desc.extent.width, desc.extent.height, 1 };
    imageInfo.format = format;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = desc.samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = desc.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (desc.isCubemap)
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VkImageViewType viewType;
    if (desc.viewType != VK_IMAGE_VIEW_TYPE_MAX_ENUM)
    {
        // Explicit view type override
        viewType = desc.viewType;
    }
    else if (desc.isCubemap)
    {
        viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    }
    else if (desc.arrayLayers > 1)
    {
        viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }
    else
    {
        viewType = VK_IMAGE_VIEW_TYPE_2D;
    }

    auto image = make_shared<Image>(_device, imageInfo, desc.aspect, viewType);
    auto texture = make_shared<Texture>(name, image, _defaultSampler);

    _renderTargets[name] = texture;
    return texture;
}

Framebuffer* Core::RenderFrame::GetOrCreateFramebuffer(const string& name, 
    RenderPass& renderPass, const vector<string>& attachmentNames, int32_t layerIndex)
{
    auto it = _framebuffers.find(name);
    if (it != _framebuffers.end())
        return it->second.get();

    vector<VkImageView> imageViews;
    VkExtent2D extent = { 0, 0 };
    
    for (const auto& attachmentName : attachmentNames)
    {
        auto texture = GetRenderTarget(attachmentName);
        if (texture)
        {
            if (layerIndex >= 0)
            {
                // Use single layer image view for array textures
                imageViews.push_back(texture->GetLayerImageView(
                    static_cast<uint32_t>(layerIndex)));
            }
            else
            {
                // Use full image view (default behavior)
                imageViews.push_back(texture->GetImageView());
            }

            if (extent.width == 0)
            {
                auto texExtent = texture->GetExtent();
                extent = { texExtent.width, texExtent.height };
            }
        }
    }

    if (imageViews.empty())
        return nullptr;

    // Create framebuffer with explicit image views
    auto framebuffer = make_unique<Framebuffer>(_device, renderPass, imageViews, extent);

    auto* result = framebuffer.get();
    _framebuffers[name] = std::move(framebuffer);
    return result;
}

Framebuffer* Core::RenderFrame::GetFramebuffer(const string& name) const
{
    auto it = _framebuffers.find(name);
    if (it != _framebuffers.end())
        return it->second.get();
    return nullptr;
}

void Core::RenderFrame::RegisterFramebuffer(const string& name, unique_ptr<Framebuffer> framebuffer)
{
    _framebuffers[name] = std::move(framebuffer);
}

DescriptorSetBuilder RenderFrame::CreateDescriptorSetBuilder(Shader& shader, uint32_t setIndex)
{
	return DescriptorSetBuilder(_device, *_descriptorPool, shader, setIndex);
}

RendererBatch* RenderFrame::GetRendererBatch() const
{
	return _renderExecutor->GetRendererBatch();
}

void RenderFrame::InitializeBatches(Scene& scene, VkExtent2D extents)
{
	_renderExecutor->InitializeBatches(scene, extents);
}