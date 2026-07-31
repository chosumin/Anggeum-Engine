#pragma once

namespace Core
{
	class Texture;
	class Buffer;

	// The state a resource was left in by the frame that last touched it.
	// Imported (persistent) resources survive across frames, so the compile
	// phase reads their entry state from here.
	// Transients always start UNDEFINED and never appear here.
	//
	// STATUS: latent infrastructure today. Every current import declares an
	// explicit entryLayout (bridge mode — legacy passes change layouts outside
	// the graph's sight), which bypasses the texture lookup, and the buffer
	// states are redundant while per-slot buffers are gated by the frame-slot
	// timeline wait. The registry becomes load-bearing with (a) swapchain
	// imports — 3 images vs 2 slots, not slot-wait aligned — and (b) history
	// resources. Endgame: once legacy consumers are gone, ImportTexture drops
	// its layout parameters entirely and this registry is the sole authority
	// for entry states.
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
