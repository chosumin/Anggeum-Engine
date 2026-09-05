#pragma once
#include "StagingRing.h"
#include "UploadJob.h"
#include "ResourceHandle.h"
#include "Foundation/Threadable.h"
#include "Utils/timer.h"

namespace Core
{
	class Device;
	class WorkerThreadManager;
	class CommandPool;
	class Texture;
	class SubMesh;
	class SyncContext;

	// Engine-owned transfer machinery: the single hand-off point between the
	// upload SCHEDULERS and the GPU - shared budget and staging
	// ring, worker-recorded jobs batched into one submit, completion tracked
	// on the upload timeline.
	//
	// Upload completion is asynchronous: Flush blocks only on worker-thread
	// recording, while GPU consumption is ordered by the transfer-timeline gate.
	class TransferContext : public Threadable
	{
	public:
		// One upload with everything that must follow ITS lifecycle: the
		// handles promoted when its submission completes.
		struct PendingUpload
		{
			unique_ptr<UploadJob> job;
			Handle<Texture> texture;
			Handle<SubMesh> subMesh;

			// Which queue this upload's batch submits on.
			QueueType lane = QueueType::Transfer;
		};

		TransferContext(Device& device, WorkerThreadManager& workerThreadManager,
			SyncContext& syncContext);
		~TransferContext();

		// For self-scheduling streamers that stage BEFORE building their job.
		StagingRing::Span AcquireStagingSpan(VkDeviceSize size)
		{
			return _stagingRing->Acquire(size);
		}

		// Retires completed uploads (promotion pump), reclaims staging spans and
		// opens a fresh upload-budget window. First transfer call of the frame.
		void BeginFrame();

		// Shared per-frame upload budget. Grants (and CONSUMES) up to
		// `requested` bytes of what remains this frame - scene admission in
		// SubmitQueued charges the same tally, so a load-heavy frame
		// automatically shrinks what later callers (terrain) may stream.
		VkDeviceSize GrantUploadBudget(VkDeviceSize requested);

		// Admission control: charges `bytes` against the frame budget, or
		// refuses. A single resource larger than the whole budget gets an
		// exclusive frame (nothing else admitted yet) rather than starving.
		bool TryAdmit(VkDeviceSize bytes);

		// Appends the upload/staging stats to the engine "Status" window.
		void OnGUI();

		// Hands one upload to the worker pool and the pending set. 
		// A pending job with the same name absorbs the call.
		void SubmitJob(PendingUpload&& upload, const string& jobName);

		// Submit the jobs whose worker RECORDING has finished, one batch per
		// lane. `waitForRecordings` first waits for the graphics-lane
		// (frame-coherent) recordings; IO-bound loads keep cooking and ride a
		// later Flush.
		void Flush(bool waitForRecordings = false);

		// How many resources the promotion pump flipped Resident since the last call.
		uint32_t TakePromotedCount();

	private:
		// Promotes + destroys in-flight batches the GPU has passed.
		void CollectCompletedJobs();

		// Uploads submitted at `value` on `lane`'s timeline.
		struct InFlightJobs
		{
			uint64_t value = 0;
			QueueType lane = QueueType::Transfer;
			vector<PendingUpload> uploads;
		};

		void SubmitBatch(InFlightJobs&& batch,
			vector<CommandBuffer*>& secondaries, QueueType lane);

		Device& _device;

		unique_ptr<CommandPool> _primaryCommandPool;
		unique_ptr<CommandPool> _graphicsPrimaryCommandPool;

		// Steady-state staging memory, recycled by timeline value. Jobs whose
		// data does not fit (initial load spike) fall back to their own
		// one-shot staging buffers.
		unique_ptr<StagingRing> _stagingRing;
		VkDeviceSize _uploadBudgetPerFrame = 16ull * 1024 * 1024;

		SyncContext& _sync;

		Core::Timer _timer;

		// Main-thread only, like every other member here: workers touch nothing
		// but their own job's atomic status, so the wait mutex Threadable owns
		// is the only synchronization this class needs.
		unordered_map<string, PendingUpload> _pendingUploads;
		deque<InFlightJobs> _inFlightJobs;

		uint32_t _promotedCount = 0;
		VkDeviceSize _frameAdmittedBytes = 0;
	};
}
