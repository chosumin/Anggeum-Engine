#pragma once
#include "ResourceHandle.h"
#include "Foundation/Job.h"

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

		// Hands the queued requests to the caller and resets the queue.
		vector<TextureUploadRequest> Take()
		{
			_queued.clear();
			return std::move(_requests);
		}

	private:
		vector<TextureUploadRequest> _requests;
		unordered_set<Handle<Texture>> _queued;
	};

	// Executes one texture upload: reads the file, stages the pixels and
	// records the copy + mip generation, all on a worker thread.
	// Oversized loads (and callers without one) fall back to a dedicated
	// one-shot staging buffer that lives and dies with the job.
	class TextureUploadJob : public Job
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
