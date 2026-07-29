#pragma once
#include "ResourceHandle.h"

namespace Core
{
	class Buffer;
	class SubMesh;

	// Bounds computed from a POSITION stream. Filled on a worker thread and applied to
	// the SubMesh / scene bounds once the upload jobs are done.
	struct GeometryBounds
	{
		glm::vec3 center{ 0.0f };
		float radius = 0.0f;
		glm::vec3 min{ FLT_MAX };
		glm::vec3 max{ -FLT_MAX };
	};

	// Two passes over the positions (extent, then radius). Called from worker threads,
	// so it touches nothing but its arguments. `stride` is the source byte stride,
	// which is tightly packed and need not match sizeof(vec3).
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

		glm::vec3 min = positionAt(0);
		glm::vec3 max = min;
		for (size_t i = 1; i < count; ++i)
		{
			glm::vec3 position = positionAt(i);
			min = glm::min(min, position);
			max = glm::max(max, position);
		}

		glm::vec3 center = (min + max) * 0.5f;

		float radius = 0.0f;
		for (size_t i = 0; i < count; ++i)
			radius = glm::max(radius, glm::distance(center, positionAt(i)));

		out.center = center;
		out.radius = radius;
		out.min = min;
		out.max = max;
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
	};

	// One-shot hand-off from resource loading to the GPU upload: ResourceManager pushes
	// resolved copies here, RenderScene::Sync turns them into transfer jobs. A
	// non-empty queue is the "geometry needs uploading" dirty state.
	class GeometryCopyQueue
	{
	public:
		void Push(GeometryCopyBatch&& batch) { _batches.push_back(std::move(batch)); }

		bool Empty() const { return _batches.empty(); }

		// Hands the queued batches to the caller and resets the queue.
		vector<GeometryCopyBatch> Take() { return std::move(_batches); }

	private:
		vector<GeometryCopyBatch> _batches;
	};
}
