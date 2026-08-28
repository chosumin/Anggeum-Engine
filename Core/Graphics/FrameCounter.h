#pragma once

namespace Core
{
	// Engine-global frame number
	// Pairs with MAX_FRAMES_IN_FLIGHT for frame-slot
	// schemes: slot = frame % MAX, and work stamped at frame N is safely
	// retired once the counter reaches N + MAX (Begin's in-flight wait).
	// Lives in its own header so non-presentation code (transfer, streaming)
	// can read frame time without depending on the frame manager.
	class FrameCounter
	{
	public:
		static void IncreaseFrame() { ++_frameNumber; }
		static uint64_t GetFrameNumber() { return _frameNumber; }
	private:
		static inline uint64_t _frameNumber = 0;
	};
}
