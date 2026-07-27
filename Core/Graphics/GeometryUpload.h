#pragma once
#include "ResourceHandle.h"

namespace Core
{
	class SubMesh;

	// Raw geometry handed from a loader to the render side. One entry per glTF mesh, so
	// its submeshes upload together in a single transfer job.
	struct SubMeshGeometry
	{
		struct Attribute { string name; uint32_t stride; vector<uint8_t> data; };

		Handle<SubMesh> subMesh;
		vector<Attribute> attributes;
		vector<uint8_t> indexData;
		VkIndexType indexType = VK_INDEX_TYPE_UINT16;
		bool hasIndex = false;
	};

	struct MeshGeometryUpload
	{
		string debugName;
		vector<SubMeshGeometry> subMeshes;
	};

	// One-shot hand-off between asset loading and the GPU upload. Loaders only depend on
	// this data sink, not on MeshBufferManager / allocation policy; RenderScene::Sync
	// drains it (see Engine::Draw).
	class GeometryUploadQueue
	{
	public:
		void Push(MeshGeometryUpload&& upload)
		{
			for (const auto& sub : upload.subMeshes)
				_queued.insert(sub.subMesh);

			_uploads.push_back(std::move(upload));
		}

		// True once a submesh's geometry is queued, so loaders can skip re-reading it
		// before the upload has happened.
		bool IsQueued(const Handle<SubMesh>& subMesh) const
		{
			return _queued.find(subMesh) != _queued.end();
		}

		bool Empty() const { return _uploads.empty(); }

		// Hands the queued uploads to the caller and resets the queue.
		vector<MeshGeometryUpload> Take()
		{
			_queued.clear();
			return std::move(_uploads);
		}

	private:
		vector<MeshGeometryUpload> _uploads;
		unordered_set<Handle<SubMesh>> _queued;
	};
}
