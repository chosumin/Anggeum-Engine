#pragma once
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class ResourceManager;
	class FrameResources;
	class FrameGraphPassContext;
	class CommandBuffer;
	class Texture;
	class Shader;
	class Pipeline;
	class Sampler;

	// Max-depth mip pyramid (R32_SFLOAT) built from a depth image.
	class HiZPyramid
	{
	public:
		HiZPyramid(ResourceManager& resourceManager, VkExtent2D extent);
		~HiZPyramid();

		static uint32_t MipLevelsFor(VkExtent2D extent);

		VkExtent2D GetExtent() const { return _extent; }
		uint32_t GetMipLevels() const { return _mipLevels; }

		// This slot's pyramid image. The mip views are created here (main
		// thread) because Build records on a worker thread.
		Handle<Texture> EnsureTexture(FrameResources& frameResources, const char* name);

		void Build(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
			Texture& depth, Texture& hiZ);

	private:
		ResourceManager& _resourceManager;
		VkExtent2D _extent;
		uint32_t _mipLevels;

		Handle<Shader> _shader;
		Handle<Pipeline> _pipeline;
		Handle<Sampler> _sampler;
	};
}
