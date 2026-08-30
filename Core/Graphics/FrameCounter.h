#pragma once

namespace Core
{
	class FrameCounter
	{
	public:
		static void IncreaseFrame() { ++_frameNumber; }
		static uint64_t GetFrameNumber() { return _frameNumber; }
	private:
		static inline uint64_t _frameNumber = 0;
	};
}
