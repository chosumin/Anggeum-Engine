#pragma once
#include "GeometryUpload.h"
#include "TextureUpload.h"
#include "StagingRing.h"
#include "Foundation/Job.h"
#include "Utils/timer.h"

namespace Core
{
	class Device;
	class WorkerThreadManager;
	class CommandPool;
	class SubMesh;

	// Engine-owned transfer context: the single hand-off point between resource
	// loading and the GPU. Request queues on top (loaders, terrain, the draw
	// batch all push requests here), transfer machinery below - worker-recorded
	// jobs batched into one submit, completion tracked on the upload timeline.
	// Sibling of RenderContext / SyncContext; the dedicated transfer queue
	// lives here when it gets enabled.
	//
	// Current stage: synchronous model. The staging ring, shared byte budget
	// and timeline-based async completion land in the follow-up steps.
	class TransferContext
	{
	public:
		TransferContext(Device& device, WorkerThreadManager& workerThreadManager);
		~TransferContext();

		GeometryCopyQueue& GetGeometryCopyQueue() { return _geometryCopies; }
		TextureUploadQueue& GetTextureUploadQueue() { return _textureUploads; }
		StagingRing& GetStagingRing() { return *_stagingRing; }

		// Once per frame, after Begin's in-flight wait: retires frame-slot
		// staging spans and opens a fresh upload-budget window.
		void BeginFrame();

		// Shared per-frame upload budget. Returns how many of `requested`
		// bytes the caller may stage this frame, measured against everything
		// already asked of the staging ring (fallback traffic included) 
		// - a scene load spike automatically starves later callers.
		VkDeviceSize GrantUploadBudget(VkDeviceSize requested);

		// Appends the upload/staging stats to the engine "Status" window.
		void OnGUI();

		// Hands a prepared job to the transfer machinery (recorded on a worker
		// thread, submitted by the next Flush). A name already pending drops
		// the new job (destroyed on return) - the queued one covers the request.
		void Enqueue(unique_ptr<Job> job, const string& jobName);

		// Drain both request queues into transfer jobs.
		void SubmitQueued();

		// Submit every enqueued job's recording as one batch and block until
		// the GPU has consumed it.
		void Flush();

		// Bounds computed by finished geometry jobs. Only valid after Flush().
		struct CompletedBounds
		{
			SubMesh* target;
			GeometryBounds bounds;
		};
		vector<CompletedBounds> TakeCompletedBounds();

	private:
		void ClearJobs();

		// A result an in-flight job is still writing into (stable address).
		struct PendingBounds
		{
			SubMesh* target;
			unique_ptr<GeometryBounds> result;
		};

		Device& _device;
		WorkerThreadManager& _workerThreadManager;

		TextureUploadQueue _textureUploads;
		GeometryCopyQueue _geometryCopies;
		vector<PendingBounds> _pendingBounds;

		unique_ptr<CommandPool> _primaryCommandPool;

		// Steady-state staging memory, recycled by timeline value. Jobs whose
		// data does not fit (initial load spike) fall back to their own
		// one-shot staging buffers.
		unique_ptr<StagingRing> _stagingRing;
		VkDeviceSize _uploadBudgetPerFrame = 16ull * 1024 * 1024;

		// Monotonic upload timeline: each Flush submit signals the next value.
		// Today Flush waits on it synchronously; the async step turns the wait
		// into per-frame counter polling that promotes completed uploads.
		VkSemaphore _timeline = VK_NULL_HANDLE;
		uint64_t _submittedValue = 0;

		condition_variable _jobWait;
		mutex _lock;
		Core::Timer _timer;
		unordered_map<string, unique_ptr<Job>> _pendingJobs;
	};
}
