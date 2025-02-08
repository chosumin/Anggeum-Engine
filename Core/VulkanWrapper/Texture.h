#pragma once

namespace Core
{
	class Image;
	class Texture
	{
	public:
		Texture(Device& device, string fileName);
		~Texture();

		VkDescriptorImageInfo GetDescriptorImageInfo();
	private:
		void CreateTextureSampler();
		void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
	private:
		Device& _device;
		unique_ptr<Image> _image;
		VkSampler _textureSampler;
		uint32_t _mipLevels;
	};
}

