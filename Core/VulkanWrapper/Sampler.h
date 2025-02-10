#pragma once

namespace Core
{
	class Sampler
	{
	public:
		static Sampler* CreateDefault(Device& device);
	public:
		Sampler(Device& device, VkSamplerCreateInfo& info);
		~Sampler();

		const VkSampler& GetSampler() { return _sampler; }
	private:
		Device& _device;
		VkSampler _sampler;
	};
}