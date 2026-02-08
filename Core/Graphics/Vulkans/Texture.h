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
		Texture(string name, shared_ptr<Image> image, shared_ptr<Sampler> sampler);
		~Texture();

		weak_ptr<Image> GetImage() { return _image; }

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
		shared_ptr<Image> _image;
		shared_ptr<Sampler> _sampler;
	};

	struct TextureBuffer
	{
		shared_ptr<Texture> texture;
		uint mipLevel;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkDescriptorImageInfo imageInfo{};

		VkWriteDescriptorSet CreateWriteDescriptorSet(uint32_t binding,
			VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			imageInfo.imageLayout = imageLayout;
			imageInfo.imageView = texture->GetImage().lock()->GetOrCreateImageView(mipLevel);

			if (descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				auto sampler = texture->GetSampler();
				if (sampler != nullptr)
					imageInfo.sampler = sampler->GetSampler();
			}
			else
			{
				imageInfo.sampler = VK_NULL_HANDLE;
			}

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstBinding = binding;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = descriptorType;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &imageInfo;

			return descriptorWrite;
		}
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

