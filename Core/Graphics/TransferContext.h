#pragma once
#include "GeometryUpload.h"
#include "TextureUpload.h"
#include "StagingRing.h"
#include "UploadJob.h"
#include "Utils/timer.h"

namespace Core
{
	class Device;
	class WorkerThreadManager;
	class CommandPool;
	class SubMesh;
	class SyncContext;

	// Engine-owned transfer context: the single hand-off point between resource
	// loading and the GPU. Request queues on top (loaders, terrain, the draw
	// batch all push requests here), transfer machinery below - worker-recorded
	// jobs batched into one submit, completion tracked on the upload timeline.
	// Sibling of RenderContext / SyncContext; the dedicated transfer queue
	// lives here when it gets enabled.
	//
	// Upload completion is asynchronous: Flush blocks only on worker-thread
	// recording, while GPU consumption is ordered by the transfer-timeline gate.
	class TransferContext
	{
	public:
		TransferContext(Device& device, WorkerThreadManager& workerThreadManager,
			SyncContext& syncContext);
		~TransferContext();

		GeometryCopyQueue& GetGeometryCopyQueue() { return _geometryCopies; }
		TextureUploadQueue& GetTextureUploadQueue() { return _textureUploads; }
		StagingRing& GetStagingRing() { return *_stagingRing; }

		// Retires completed uploads (promotion pump), reclaims staging spans and
		// opens a fresh upload-budget window. First transfer call of the frame.
		void BeginFrame();

		// Shared per-frame upload budget. Grants (and CONSUMES) up to
		// `requested` bytes of what remains this frame - scene admission in
		// SubmitQueued charges the same tally, so a load-heavy frame
		// automatically shrinks what later callers (terrain) may stream.
		VkDeviceSize GrantUploadBudget(VkDeviceSize requested);

		// Appends the upload/staging stats to the engine "Status" window.
		void OnGUI();

		// Drain both request queues into transfer jobs.
		void SubmitQueued();

		// For streaming clients that build their own upload jobs.
		// The caller has already charged the budget and acquired its staging.
		void SubmitStreamingJob(unique_ptr<UploadJob> job, const string& jobName);

		// Submit the jobs whose worker RECORDING has finished, as one batch.
		// `waitForRecordings` first waits for the must-land jobs' recordings;
		// IO-bound loads keep cooking and ride a later Flush.
		void Flush(bool waitForRecordings = false);

		// How many resources the promotion pump flipped Resident since the last call.
		uint32_t TakePromotedCount();

	private:
		// One upload with everything that must follow ITS lifecycle: the
		// handles promoted when its submission completes. 
		struct PendingUpload
		{
			unique_ptr<UploadJob> job;
			Handle<Texture> texture;
			Handle<SubMesh> subMesh;

			// Must be submitted the frame it was enqueued (table fills back a
			// draw set that already changed CPU-side; terrain tiles publish with
			// their tables). Always memcpy recordings - Flush's wait never
			// blocks on file IO.
			bool mustLand = false;
		};

		// Admission control: charges `bytes` against the frame budget, or
		// refuses. A single resource larger than the whole budget gets an
		// exclusive frame (nothing else admitted yet) rather than starving.
		bool TryAdmit(VkDeviceSize bytes);

		void EnqueueUpload(PendingUpload&& upload, const string& jobName);

		// Promotes + destroys in-flight batches the GPU has passed.
		void CollectCompletedJobs(uint64_t completedValue);

		// Uploads submitted at `value`, awaiting GPU completion before their
		// resources promote and their jobs (fallback staging included) die.
		struct InFlightJobs
		{
			uint64_t value;
			vector<PendingUpload> uploads;
		};

		Device& _device;
		WorkerThreadManager& _workerThreadManager;

		TextureUploadQueue _textureUploads;
		GeometryCopyQueue _geometryCopies;

		unique_ptr<CommandPool> _primaryCommandPool;

		// Steady-state staging memory, recycled by timeline value. Jobs whose
		// data does not fit (initial load spike) fall back to their own
		// one-shot staging buffers.
		unique_ptr<StagingRing> _stagingRing;
		VkDeviceSize _uploadBudgetPerFrame = 16ull * 1024 * 1024;

		SyncContext& _sync;

		condition_variable _jobWait;
		mutex _lock;
		Core::Timer _timer;
		unordered_map<string, PendingUpload> _pendingJobs;
		deque<InFlightJobs> _inFlightJobs;

		uint32_t _promotedCount = 0;
		VkDeviceSize _frameAdmittedBytes = 0;
	};
}
