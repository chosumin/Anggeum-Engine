#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "RenderPass.h"
#include "GeometryRenderPass.h"
#include "ShadowRenderPass.h"
#include "Scene.h"
#include "SwapChain.h"
#include "Utility.h"
#include "RenderContext.h"
using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(
	Device& device, Scene& scene, SwapChain& swapChain)
	:_device(device)
{
	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::AddResizeCallback(a);

	_msaaSamples = GetMaxUsableSampleCount();

	auto extent = swapChain.GetSwapChainExtent();

	_renderTargets.push_back(CreateColorRenderTarget(extent, swapChain.GetImageFormat()));
	_renderTargets.push_back(CreateDepthRenderTarget(extent));
	_renderTargets.push_back(CreateDepthRenderTarget(extent));

	auto shadowRenderPass = new ShadowRenderPass(
		device, scene, swapChain, _renderTargets[2].get());
	AddRenderPass(shadowRenderPass);

	auto geometryRenderPass = new GeometryRenderPass(
		device, scene, swapChain, _renderTargets[0].get(), _renderTargets[1].get());
	AddRenderPass(geometryRenderPass);
}

Core::ForwardRenderPipeline::~ForwardRenderPipeline()
{
	Cleanup();

	for (auto&& renderPass : _renderPasses)
	{
		delete(renderPass);
	}

	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::RemoveResizeCallback(a);
}

void ForwardRenderPipeline::Prepare()
{
	for (auto&& renderPass : _renderPasses)
	{
		renderPass->Prepare();
	}
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
	auto device = _device.GetDevice();
	for (size_t i = 0; i < _renderTargets.size(); ++i)
	{
		vkDestroyImageView(device, _renderTargets[i]->ImageView, nullptr);
		vkDestroyImage(device, _renderTargets[i]->Image, nullptr);
		vkFreeMemory(device, _renderTargets[i]->ImageMemory, nullptr);
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

	if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

unique_ptr<RenderTarget> Core::ForwardRenderPipeline::CreateRenderTarget(VkExtent2D extent, VkFormat format, VkImageLayout layout, VkImageUsageFlags usageFlags)
{
	VkImage image;
	VkImageView imageView;
	VkDeviceMemory memory;

	Utility::CreateImage(_device, extent.width, extent.height, 1,
		_msaaSamples, format, VK_IMAGE_TILING_OPTIMAL,
		usageFlags,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

	imageView = Utility::CreateImageView(_device, image, format, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

	auto renderTarget = make_unique<RenderTarget>();
	renderTarget->Format = format;
	renderTarget->Layout = layout;
	renderTarget->Image = image;
	renderTarget->ImageMemory = memory;
	renderTarget->ImageView = imageView;
	renderTarget->UsageFlags = usageFlags;
	renderTarget->SampleCount = _msaaSamples;

	return move(renderTarget);
}

unique_ptr<RenderTarget> Core::ForwardRenderPipeline::CreateDepthRenderTarget(VkExtent2D extent)
{
	VkImage image;
	VkImageView imageView;
	VkDeviceMemory memory;

	auto depthFormat = _device.FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	Utility::CreateImage(_device, extent.width, extent.height, 1,
		_msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

	imageView = Utility::CreateImageView(_device,
		image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

	auto renderTarget = make_unique<RenderTarget>();
	renderTarget->Format = depthFormat;
	renderTarget->Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	renderTarget->Image = image;
	renderTarget->ImageMemory = memory;
	renderTarget->ImageView = imageView;
	renderTarget->SampleCount = _msaaSamples;

	return move(renderTarget);
}

unique_ptr<RenderTarget> Core::ForwardRenderPipeline::CreateColorRenderTarget(VkExtent2D extent, VkFormat format)
{
	VkImage image;
	VkImageView imageView;
	VkDeviceMemory memory;

	//VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : gpu virtual address and not physical memory pages.
	Utility::CreateImage(_device, extent.width, extent.height, 1,
		_msaaSamples, format, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

	imageView = Utility::CreateImageView(_device,
		image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	auto renderTarget = make_unique<RenderTarget>();
	renderTarget->Format = format;
	renderTarget->Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	renderTarget->Image = image;
	renderTarget->ImageMemory = memory;
	renderTarget->ImageView = imageView;
	renderTarget->SampleCount = _msaaSamples;

	return move(renderTarget);
}
