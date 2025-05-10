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
		VkDescriptorImageInfo GetDescriptorImageInfo();

		uint32_t GetMipLevels() const;
		uint32_t GetLayers() const;

		VkFormat GetFormat() const
		{
			return _image->GetFormat();
		}

		VkImageView GetImageView() const
		{
			return _image->GetImageView();
		}

		VkSampleCountFlagBits GetSampleCount() const
		{
			return _image->GetSampleCount();
		}

		VkExtent3D GetExtent() const
		{
			return _image->GetExtent();
		}

		string& GetName() { return _name; }
	private:
		string _name;
		shared_ptr<Image> _image;
		shared_ptr<Sampler> _sampler;
	};
}

