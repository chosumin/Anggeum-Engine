#pragma once

namespace Core
{
	struct UniformBufferLayoutBinding
	{
	public:
		UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize bufferSize);

		uint32_t Binding;
		VkShaderStageFlags Stage;
		VkDeviceSize BufferSize;
	};
}
