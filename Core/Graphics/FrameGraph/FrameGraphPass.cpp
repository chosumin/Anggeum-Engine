#include "stdafx.h"
#include "FrameGraphPass.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"

namespace Core
{
	Texture& FrameGraphPassContext::GetTexture(FGTexture handle) const
	{
		assert(handle.IsValid() && "invalid frame graph texture handle");
		assert(handle.index < _declared.size() && _declared[handle.index] &&
			"texture was not declared by this pass");

		Texture* texture = _textures[handle.index];
		assert(texture != nullptr && "texture was not realized");
		return *texture;
	}

	Buffer& FrameGraphPassContext::GetBuffer(FGBuffer handle) const
	{
		assert(handle.IsValid() && "invalid frame graph buffer handle");
		assert(handle.index < _declared.size() && _declared[handle.index] &&
			"buffer was not declared by this pass");

		Buffer* buffer = _buffers[handle.index];
		assert(buffer != nullptr && "buffer was not realized");
		return *buffer;
	}

	DescriptorSetBuilder FrameGraphPassContext::CreateDescriptorSetBuilder(
		Shader& shader, uint32_t setIndex) const
	{
		return DescriptorSetBuilder(_device, _descriptorPool, shader, setIndex);
	}

	void FrameGraphPassContext::BeginRendering(CommandBuffer& commandBuffer, uint32_t variant) const
	{
		assert(variant < MaxRenderingVariants);
		assert(_rendering[variant].valid && "pass declared no attachments for this variant");

		commandBuffer.BeginRendering(_rendering[variant]);
	}

	void FrameGraphPassContext::EndRendering(CommandBuffer& commandBuffer) const
	{
		commandBuffer.EndRendering();
	}

	VkExtent2D FrameGraphPassContext::GetRenderArea(uint32_t variant) const
	{
		assert(variant < MaxRenderingVariants && _rendering[variant].valid);
		return _rendering[variant].renderArea;
	}
}
