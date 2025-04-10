#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "VulkanWrapper/RenderPass.h"
#include "VulkanWrapper/SwapChain.h"
#include "Core/RenderPasses/GeometryRenderPass.h"
#include "Core/RenderPasses/ShadowRenderPass.h"
#include "Scene.h"
#include "Utils/Utility.h"
#include "RenderContext.h"

#include "RenderPasses/SkyPregenerationRenderPass.h"

using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(
	Device& device, Scene& scene, SwapChain& swapChain)
	:_device(device)
{
	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::AddResizeCallback(a);

	_msaaSamples = GetMaxUsableSampleCount();

	CreateSampler();

	auto extent = swapChain.GetSwapChainExtent();

	_renderTargets.push_back(CreateColorRenderTarget(extent, swapChain.GetImageFormat(), false));
	_renderTargets.push_back(CreateDepthRenderTarget(extent, false, _msaaSamples));
	_renderTargets.push_back(CreateDepthRenderTarget(extent, true, VK_SAMPLE_COUNT_1_BIT));

	CreatePreSkyTextures();

	auto shadowRenderPass = new ShadowRenderPass(
		device, scene, swapChain, _renderTargets[2].get());
	AddRenderPass(shadowRenderPass);

	auto geometryRenderPass = new GeometryRenderPass(
		device, scene, swapChain, 
		_renderTargets[0].get(), _renderTargets[1].get(), 
		_renderTargets[2].get(), _renderTargets[3].get(), _renderTargets[4].get());
	geometryRenderPass->SetBuffer(shadowRenderPass->GetShadowBuffer());

	AddRenderPass(geometryRenderPass);
}

Core::ForwardRenderPipeline::~ForwardRenderPipeline()
{
	Cleanup();

	for (auto&& renderPass : _renderPasses)
	{
		delete(renderPass);
	}

	delete(_sampler);

	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::RemoveResizeCallback(a);
}

void ForwardRenderPipeline::Prepare()
{
	for (auto&& renderPass : _renderPasses)
	{
		renderPass->Prepare();
	}
	
	//todo : offscreen renderpass
}

void ForwardRenderPipeline::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	for (auto&& renderPass : _renderPasses)
	{
		renderPass->Draw(commandBuffer, currentFrame, imageIndex);
	}
}

RenderPass& Core::ForwardRenderPipeline::GetRenderPass(uint32_t index)
{
	return *_renderPasses[index];
}

void Core::ForwardRenderPipeline::Cleanup()
{
	for (auto&& renderTarget : _renderTargets)
	{
		renderTarget->Cleanup();
	}

	_renderTargets.clear();
}

void Core::ForwardRenderPipeline::Resize(SwapChain& swapChain)
{
	Cleanup();

	VkExtent2D extent = swapChain.GetSwapChainExtent();

	/*if (_color != nullptr)
	{
		CreateColorRenderTarget(extent, _color->Format, _color->LoadOp, _color->StoreOp, _color->InitialLayout);
	}

	if (_depth != nullptr)
	{
		CreateDepthRenderTarget(extent, _depth->LoadOp, _depth->StoreOp);
	}

	for (auto& renderTarget : _inputRenderTargets)
	{
		CreateRenderTarget(extent, renderTarget->Format, renderTarget->Layout, renderTarget->UsageFlags, renderTarget->LoadOp, renderTarget->StoreOp);
	}*/
}

VkSampleCountFlagBits Core::ForwardRenderPipeline::GetMaxUsableSampleCount()
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(
		_device.GetPhysicalDevice(),
		&physicalDeviceProperties);

	VkSampleCountFlags counts =
		physicalDeviceProperties.limits.framebufferColorSampleCounts &
		physicalDeviceProperties.limits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

unique_ptr<Texture> Core::ForwardRenderPipeline::CreateRenderTarget(VkExtent2D extent, VkFormat format, VkImageLayout layout, VkImageUsageFlags usageFlags)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { extent.width, extent.height, 1 };
	imageInfo.format = format;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = _msaaSamples;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = usageFlags;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	
	Image* image = new Image(_device, imageInfo, layout, VK_IMAGE_ASPECT_COLOR_BIT);

	unique_ptr<Texture> renderTarget = make_unique<Texture>("render target", image, _sampler);
	return move(renderTarget);
}

unique_ptr<Texture> Core::ForwardRenderPipeline::CreateDepthRenderTarget(VkExtent2D extent, bool isUsedAsSource, VkSampleCountFlagBits sampleCount)
{
	auto depthFormat = _device.FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	VkImageUsageFlags flags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (isUsedAsSource)
		flags |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { extent.width, extent.height, 1 };
	imageInfo.format = depthFormat;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = sampleCount;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = flags;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto image = new Image(_device, imageInfo, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

	unique_ptr<Texture> renderTarget = make_unique<Texture>("depth target", image, _sampler);
	return move(renderTarget);
}

unique_ptr<Texture> Core::ForwardRenderPipeline::CreateColorRenderTarget(VkExtent2D extent, VkFormat format, bool isUsedAsSource)
{
	VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (isUsedAsSource)
		flags |= VK_IMAGE_USAGE_SAMPLED_BIT;

	//VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : gpu virtual address and not physical memory pages.
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { extent.width, extent.height, 1 };
	imageInfo.format = format;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = _msaaSamples;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = flags;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto image = new Image(_device, imageInfo, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	unique_ptr<Texture> renderTarget = make_unique<Texture>("color target", image, _sampler);

	return move(renderTarget);
}

void Core::ForwardRenderPipeline::CreateSampler()
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &properties);

	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_TRUE; //usually used for percentage-closer filtering on shadow maps.
	//samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

	_sampler = new Sampler(_device, samplerInfo);
}

void Core::ForwardRenderPipeline::CreatePreSkyTextures()
{
	uint32_t size = 64;
	const uint32_t numMips = static_cast<uint32_t>(floor(std::log2(size))) + 1;
	VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
	
	{
		//Offscreen texture to blit to the cubemap
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { size , size , 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		auto image = new Image(_device, imageInfo,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT);
		unique_ptr<Texture> offscreen = make_unique<Texture>("offscreen", image, _sampler);
		_renderTargets.push_back(move(offscreen));
	}

	{
		//Environment texture
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { size , size , 1 };
		imageInfo.mipLevels = numMips;
		imageInfo.arrayLayers = 6;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		auto image = new Image(_device, imageInfo,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_VIEW_TYPE_CUBE);

		unique_ptr<Texture> cubemap = make_unique<Texture>("cubemap", image, _sampler);
		_renderTargets.push_back(move(cubemap));
	}
}
