#pragma once
#include "TextureUpload.h"
#include "GeometryUpload.h"

namespace Core
{
	class Device;
	class TransferContext;

	// The scene-asset upload scheduler. Loaders and the draw batch push
	// lightweight REQUESTS into its queues at any time.
	class AssetStreamer
	{
	public:
		AssetStreamer(Device& device, TransferContext& transfer);

		void Push(TextureUploadRequest&& request)
		{
			_textureUploads.Push(std::move(request));
		}
		void Push(GeometryCopyBatch&& batch)
		{
			_geometryCopies.Push(std::move(batch));
		}

		// Drain both request queues into transfer jobs, within budget.
		void SubmitQueued();

	private:
		Device& _device;
		TransferContext& _transfer;

		TextureUploadQueue _textureUploads;
		GeometryCopyQueue _geometryCopies;
	};
}
