#pragma once
#include "ResourceHandle.h"

namespace Core
{
	class Buffer;

	// One copy into a pool-owned buffer at a byte offset.
	struct BufferUploadRegion
	{
		Handle<Buffer> buffer;
		vector<uint8_t> data;
		VkDeviceSize offset = 0;
	};

	// A group of regions that should upload together in a single transfer job
	// (e.g. all buffers of one skybox mesh).
	struct BufferUpload
	{
		string debugName;
		vector<BufferUploadRegion> regions;
	};

	// One-shot hand-off between asset loading and the GPU upload: loaders push raw
	// data for standalone (non-global) buffers, RenderScene::Sync drains it into
	// transfer jobs.
	class BufferUploadQueue
	{
	public:
		void Push(BufferUpload&& upload) { _uploads.push_back(std::move(upload)); }

		bool Empty() const { return _uploads.empty(); }

		// Hands the queued uploads to the caller and resets the queue.
		vector<BufferUpload> Take() { return std::move(_uploads); }

	private:
		vector<BufferUpload> _uploads;
	};
}
