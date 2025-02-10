#pragma once

namespace Core
{
	class Image;
	class Sampler;
	class Texture
	{
	public:
		Texture(Device& device, string name, Image* image, Sampler* sampler);
		~Texture();

		VkDescriptorImageInfo GetDescriptorImageInfo();
	private:
		string _name;
		Device& _device;
		Image* _image;
		Sampler* _sampler;
	};
}

