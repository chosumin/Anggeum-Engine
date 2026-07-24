#pragma once
#include "Image.h"
#include "Sampler.h"

namespace Core
{
	class Image;
	class Sampler;

	class Texture
	{
	public:
		// Texture owns its Image 1:1. The image is built by the creator (cache,
		// render target, pass) and moved in.
		Texture(string name, unique_ptr<Image> image, shared_ptr<Sampler> sampler);
		~Texture();

		Image& GetImage() { return *_image; }

		uint32_t GetMipLevels() const;
		uint32_t GetLayers() const;

		VkFormat GetFormat() const
		{
			return _image->GetFormat();
		}

		VkImageView GetImageView() const
		{
			return _image->GetOrCreateImageView(0);
		}

		VkImageView GetLayerImageView(uint32_t layerIndex)
		{
			return _image->GetOrCreateLayerImageView(layerIndex);
		}

		VkSampleCountFlagBits GetSampleCount() const
		{
			return _image->GetSampleCount();
		}

		VkExtent3D GetExtent() const
		{
			return _image->GetExtent();
		}

		shared_ptr<Sampler> GetSampler() { return _sampler; }

		string& GetName() { return _name; }
	private:
		string _name;
		unique_ptr<Image> _image;
		shared_ptr<Sampler> _sampler;
	};

	struct TextureBuffer
	{
		shared_ptr<Texture> texture;
		uint mipLevel;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkDescriptorImageInfo imageInfo{};

		VkWriteDescriptorSet CreateWriteDescriptorSet(uint32_t binding,
			VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	};

	struct TextureBufferLayoutBinding
	{
	public:
		TextureBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage,
			VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			: Binding(binding), Stage(stage), DescriptorType(descriptorType) {
		}

		uint32_t Binding;
		VkShaderStageFlags Stage;
		VkDescriptorType DescriptorType;
	};
}

