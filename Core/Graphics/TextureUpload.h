#pragma once
#include "ResourceHandle.h"
#include "UploadJob.h"

namespace Core
{
	class Device;
	class Texture;
	class Buffer;
	class StagingRing;

	// A texture whose pixel data should be read from `filePath` and uploaded. The job
	// itself reads the file on a worker thread, so only the path is handed over.
	struct TextureUploadRequest
	{
		Handle<Texture> texture;
		string filePath;

		// Bytes this upload will stage, for budget admission.
		VkDeviceSize stagingBytes = 0;
	};

	// One-shot hand-off between asset loading and the GPU upload: loaders push
	// requests, RenderScene::Sync drains them into transfer jobs. Deduplicates while
	// pending (the same texture is often referenced by several materials).
	class TextureUploadQueue
	{
	public:
		void Push(TextureUploadRequest&& request)
		{
			if (_queued.find(request.texture) != _queued.end())
				return;

			_queued.insert(request.texture);
			_requests.push_back(std::move(request));
		}

		bool Empty() const { return _requests.empty(); }

		// FIFO partial draining, so admission control can stop at the first
		// request the frame budget cannot cover and leave the rest queued.
		const TextureUploadRequest& Front() const { return _requests.front(); }

		TextureUploadRequest PopFront()
		{
			TextureUploadRequest request = std::move(_requests.front());
			_requests.erase(_requests.begin());
			_queued.erase(request.texture);
			return request;
		}

	private:
		vector<TextureUploadRequest> _requests;
		unordered_set<Handle<Texture>> _queued;
	};

	// Executes one texture upload: reads the file, stages the pixels and
	// records the copy + mip generation, all on a worker thread.
	// Oversized loads (and callers without one) fall back to a dedicated
	// one-shot staging buffer that lives and dies with the job.
	class TextureUploadJob : public UploadJob
	{
	public:
		TextureUploadJob(Device& device, Texture& dstTexture, string filePath,
			StagingRing* stagingRing = nullptr);
		~TextureUploadJob();

		void Execute() override;

	private:
		Device& _device;
		string _filePath;

		Texture& _dstTexture;
		StagingRing* _stagingRing;
		unique_ptr<Buffer> _stagingBuffer;
	};
}
