#pragma once
#include "ResourceHandle.h"
#include "UploadJob.h"

namespace Core
{
	class Device;
	class Buffer;
	class SubMesh;
	class StagingRing;

	struct GeometryBounds
	{
		glm::vec3 min{ 0.0f };
		glm::vec3 max{ 0.0f };
	};

	inline void ComputeGeometryBounds(const uint8_t* data, size_t byteSize,
		uint32_t stride, GeometryBounds& out)
	{
		size_t count = (stride > 0) ? byteSize / stride : 0;
		if (count == 0)
			return;

		auto positionAt = [&](size_t i)
		{
			glm::vec3 position;
			memcpy(&position, data + i * stride, sizeof(float) * 3);
			return position;
		};

		out.min = positionAt(0);
		out.max = out.min;
		for (size_t i = 1; i < count; ++i)
		{
			glm::vec3 position = positionAt(i);
			out.min = glm::min(out.min, position);
			out.max = glm::max(out.max, position);
		}
	}

	// Raw geometry for one submesh, handed to ResourceManager at load time. Space is
	// reserved immediately (so the SubMesh is fully formed), and only the data copy
	// is deferred — the same shape as a bindless texture getting its slot up front.
	struct SubMeshGeometry
	{
		struct Attribute { string name; uint32_t stride; vector<uint8_t> data; };

		vector<Attribute> attributes;
		vector<uint8_t> indexData;
		VkIndexType indexType = VK_INDEX_TYPE_UINT16;
		bool hasIndex = false;
	};

	// A resolved copy: destination buffer + byte offset already decided.
	struct GeometryCopy
	{
		Handle<Buffer> destination;
		vector<uint8_t> data;
		VkDeviceSize offset = 0;

		// Non-zero on the POSITION copy: its source stride, marking it as the stream to
		// compute bounds from (done by the upload job, off the main thread).
		uint32_t boundsStride = 0;
	};

	// Copies that should upload together in a single transfer job.
	struct GeometryCopyBatch
	{
		string debugName;
		vector<GeometryCopy> copies;

		// Where the computed bounds land. Resolved on the main thread; the pool owns
		// the SubMesh for the app's lifetime, so the pointer stays valid.
		SubMesh* boundsTarget = nullptr;

		Handle<SubMesh> subMesh;
	};

	// One-shot hand-off from resource loading to the GPU upload.
	// A non-empty queue is the "geometry needs uploading" dirty state.
	class GeometryCopyQueue
	{
	public:
		void Push(GeometryCopyBatch&& batch) { _batches.push_back(std::move(batch)); }

		bool Empty() const { return _batches.empty(); }

		const GeometryCopyBatch& Front() const { return _batches.front(); }

		GeometryCopyBatch PopFront()
		{
			GeometryCopyBatch batch = std::move(_batches.front());
			_batches.erase(_batches.begin());
			return batch;
		}

	private:
		vector<GeometryCopyBatch> _batches;
	};

	// Executes one batch: packs every copy into a single staging span and
	// records the copies out of its sub-ranges, so a mesh upload is one
	// worker-thread task with one staging allocation.
	class GeometryUploadJob : public UploadJob
	{
	public:
		GeometryUploadJob(Device& device, GeometryCopyBatch&& batch);
		~GeometryUploadJob();

		void Execute() override;

	private:
		Device& _device;
		GeometryCopyBatch _batch;
		vector<Buffer*> _destinations;   // resolved 1:1 with _batch.copies
		unique_ptr<Buffer> _stagingBuffer;
	};
}
