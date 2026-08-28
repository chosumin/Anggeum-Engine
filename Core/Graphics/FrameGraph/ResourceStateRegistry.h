#pragma once

namespace Core
{
	class Texture;
	class Buffer;

	// The state a resource was left in by the frame that last touched it.
	// Imported (persistent) resources survive across frames, so the compile
	// phase reads their entry state from here — this registry is the sole
	// entry-state authority.
	struct FGResourceState
	{
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 access = VK_ACCESS_2_NONE;
	};

	class FGResourceStateRegistry
	{
	public:
		// Falls back to `entry` when the texture has never been seen (first frame).
		FGResourceState GetTextureState(Texture& texture, const FGResourceState& entry) const;
		void SetTextureState(Texture& texture, const FGResourceState& state);

		FGResourceState GetBufferState(Buffer& buffer) const;
		void SetBufferState(Buffer& buffer, const FGResourceState& state);

		void Clear() { _images.clear(); _buffers.clear(); }

	private:
		// Keyed by the underlying VkImage/VkBuffer handle: per-slot resources are
		// distinct objects, so their cross-frame states stay separate.
		unordered_map<uint64_t, FGResourceState> _images;
		unordered_map<uint64_t, FGResourceState> _buffers;
	};
}
