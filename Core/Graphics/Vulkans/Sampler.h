#pragma once

namespace Core
{
	struct SamplerCreateInfo
	{
		VkFilter minFilter;
		VkFilter magFilter;
		VkSamplerAddressMode wrapS;
		VkSamplerAddressMode wrapT;
		VkSamplerMipmapMode mipmapMode;

        bool operator==(const SamplerCreateInfo& other) const
        {
			return minFilter == other.minFilter &&
				magFilter == other.magFilter &&
				wrapS == other.wrapS &&
				wrapT == other.wrapT &&
				mipmapMode == other.mipmapMode;
		}
	};

	struct SamplerCreateInfoHasher {
        std::size_t operator()(const SamplerCreateInfo& p) const 
		{
            std::size_t seed = 0;
            std::hash<int> hasher;
            seed ^= hasher(p.minFilter) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hasher(p.magFilter) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hasher(p.wrapS) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hasher(p.wrapT) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= hasher(p.mipmapMode) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
	};

	class Sampler
	{
	public:
		Sampler(Device& device, SamplerCreateInfo info);
		~Sampler();

		const VkSampler& GetSampler() { return _sampler; }
		SamplerCreateInfo& GetCreateInfo() { return _info; }
	private:
		Device& _device;
		VkSampler _sampler;

		SamplerCreateInfo _info;
	};
}