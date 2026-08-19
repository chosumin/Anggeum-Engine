#pragma once
#include "GeometryUpload.h"
#include "TextureUpload.h"

namespace Core
{
	class Device;
	class TransferContext;
	class SubMesh;

	// The single hand-off point between resource loading and the GPU transfer machinery. 
	//
	// Skeleton stage: internally delegates to TransferContext's synchronous
	// model. The staging ring, shared byte budget and timeline-based async
	// completion land here in the follow-up steps.
	class UploadScheduler
	{
	public:
		UploadScheduler(Device& device, TransferContext& transfer);

		GeometryCopyQueue& GetGeometryCopyQueue() { return _geometryCopies; }
		TextureUploadQueue& GetTextureUploadQueue() { return _textureUploads; }

		// The transfer machinery, for the callers that still enqueue their own jobs; 
		// they migrate here later.
		TransferContext& GetTransferContext() { return _transfer; }

		// Drain both queues into transfer jobs.
		void SubmitQueued();

		// Block until every submitted job has completed.
		void Flush();

		// Bounds computed by finished geometry jobs. Only valid after Flush().
		struct CompletedBounds
		{
			SubMesh* target;
			GeometryBounds bounds;
		};
		vector<CompletedBounds> TakeCompletedBounds();

	private:
		// A result an in-flight job is still writing into (stable address).
		struct PendingBounds
		{
			SubMesh* target;
			unique_ptr<GeometryBounds> result;
		};

		Device& _device;
		TransferContext& _transfer;

		TextureUploadQueue _textureUploads;
		GeometryCopyQueue _geometryCopies;
		vector<PendingBounds> _pendingBounds;
	};
}
