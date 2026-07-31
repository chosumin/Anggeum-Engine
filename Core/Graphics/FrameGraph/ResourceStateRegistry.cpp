#include "stdafx.h"
#include "ResourceStateRegistry.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/Image.h"

namespace Core
{
	FGResourceState FGResourceStateRegistry::GetTextureState(Texture& texture,
		const FGResourceState& entry) const
	{
		auto it = _images.find(reinterpret_cast<uint64_t>(texture.GetImage().GetImage()));
		return it != _images.end() ? it->second : entry;
	}

	void FGResourceStateRegistry::SetTextureState(Texture& texture, const FGResourceState& state)
	{
		_images[reinterpret_cast<uint64_t>(texture.GetImage().GetImage())] = state;
	}

	FGResourceState FGResourceStateRegistry::GetBufferState(Buffer& buffer) const
	{
		auto it = _buffers.find(reinterpret_cast<uint64_t>(buffer.GetBuffer()));
		return it != _buffers.end() ? it->second : FGResourceState{};
	}

	void FGResourceStateRegistry::SetBufferState(Buffer& buffer, const FGResourceState& state)
	{
		_buffers[reinterpret_cast<uint64_t>(buffer.GetBuffer())] = state;
	}
}
