#include "stdafx.h"
#include "Sampler.h"

Core::Sampler* Core::Sampler::CreateDefault(Device& device)
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);

	samplerInfo.anisotropyEnable = VK_TRUE;
	//lower value results in better performance, but lower quality results.
	samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE; //usually used for percentage-closer filtering on shadow maps.
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;

	//HACK : hardcoded
	samplerInfo.maxLod = numeric_limits<float>::max();

	auto sampler = new Sampler(device, samplerInfo);

	return sampler;
}

Core::Sampler::Sampler(Device& device, VkSamplerCreateInfo& info)
	:_device(device)
{
	if (vkCreateSampler(_device.GetDevice(), &info, nullptr, &_sampler) != VK_SUCCESS)
	{
		throw runtime_error("failed to create texture sampler!");
	}
}

Core::Sampler::~Sampler()
{
	auto device = _device.GetDevice();

	vkDestroySampler(device, _sampler, nullptr);
}
