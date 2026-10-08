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

		void Push(TextureUploadRequest&& request);
		void Push(GeometryCopyBatch&& batch);

		// Drain the request queues into transfer jobs, within budget.
		void SubmitQueued();

	private:
		void SubmitQueuedTextures();
		void SubmitQueuedGeometry();

		Device& _device;
		TransferContext& _transfer;

		TextureUploadQueue _textureUploads;
		GeometryCopyQueue _geometryCopies;
	};
}
