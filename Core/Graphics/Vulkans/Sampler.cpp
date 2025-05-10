#include "stdafx.h"
#include "Sampler.h"

Core::Sampler::Sampler(Device& device, SamplerCreateInfo info)
	:_device(device), _info(info)
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

	samplerInfo.minFilter = info.minFilter;
	samplerInfo.magFilter = info.magFilter;

	samplerInfo.addressModeU = info.wrapS;
	samplerInfo.addressModeV = info.wrapT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	samplerInfo.mipmapMode = info.mipmapMode;

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);

	samplerInfo.anisotropyEnable = VK_TRUE;
	//lower value results in better performance, but lower quality results.
	samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE; //usually used for percentage-closer filtering on shadow maps.
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;

	//HACK : hardcoded.
	samplerInfo.maxLod = numeric_limits<float>::max();

	if (vkCreateSampler(_device.GetDevice(), &samplerInfo, nullptr, &_sampler) != VK_SUCCESS)
	{
		throw runtime_error("failed to create texture sampler!");
	}
}

Core::Sampler::~Sampler()
{
	auto device = _device.GetDevice();

	vkDestroySampler(device, _sampler, nullptr);
}