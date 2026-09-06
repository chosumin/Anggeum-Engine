#pragma once
#include "IndirectDrawBuffer.h"
#include "BufferObjects.h"
#include "ResourceHandle.h"
#include "ResourcePool.h"

namespace Core
{
	class Material;
	class SubMesh;
	class Mesh;
	class Transform;
	class Buffer;
	class Scene;
	class ResourceManager;
	class FrameResources;

	struct TransformBatch
	{
		// Persistent (handle pattern), owned via ResourceManager; resolve with .Get().
		Handle<Buffer> TransformBuffer;
	};

	struct SubMeshBatch
	{
		Handle<SubMesh> SubMesh;
		vector<uint> Transforms;   // entity ids, one per instance
	};

	struct MaterialBatch
	{
		Handle<Material> Material;
		unordered_map<string, SubMeshBatch> SubMeshBatches;
	};

	// RendererBatch: the application-wide GPU-driven draw set. Part of RenderScene
	// (owned by Engine), a single instance shared by every frame-in-flight. Its buffers
	// are read-only inputs to the culling passes, so one shared copy is safe (the
	// mutable culling outputs are per-frame-slot graph buffers). Rebuilt from the scene by
	// Prepare(), which RenderScene::Sync calls only when the scene structure changed.
	class RendererBatch
	{
	public:
		RendererBatch(Device& device, ResourceManager& resourceManager);
		~RendererBatch();

		// Marks the draw set stale — call after adding/removing scene meshes. Kept here
		// rather than on Scene: which membership changes matter is a rendering concern,
		// and it keeps every manager owning its own dirty state.
		void MarkDirty() { _dirty = true; }

		// Bumped every time the draw set is rebuilt. Holders that sized themselves
		// against the batch (the culling passes) compare this to notice they went stale
		uint64_t GetRevision() const { return _revision; }

		// Rebuild the whole draw set from the current scene membership.
		void Sync(Scene& scene, VkExtent2D extents);

		void QueuePendingInit(FrameResources& frameResources);

		Buffer& GetObjectDataBuffer() const { return _objectDataBuffer.Get(); }
		Buffer& GetIndirectCommandBuffer() const { return _indirectCommandBuffer.Get(); }
		Buffer& GetMaterialIndexBuffer() const { return _materialIndexBuffer.Get(); }
		uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
		uint32_t GetInstanceCount() const { return _instanceCount; }
		Buffer& GetInstanceBuffer() const { return _instanceBuffer.Get(); }
		const IndirectDrawBuffer& GetIndirectDrawBuffer() const { return _indirectDrawBuffer; }
		TransformBatch& GetTransformBatch() { return _transformBatch; }
		VkExtent2D GetExtents() const { return _extents; }

	private:
		void AddMesh(uint entityId, Handle<Material> material, Handle<SubMesh> subMesh);
		void InitializeFromScene(Scene& scene);
		void RebuildGpuBuffers();

		// Create the buffer on first use, resize it in place afterwards so the handle
		// stays valid. Callers must ensure no frame is in flight when resizing.
		Handle<Buffer> AcquirePersistentBuffer(Handle<Buffer> current,
			const struct BufferDesc& desc, const string& name);

	private:
		Device& _device;
		ResourceManager& _resourceManager;

		TransformBatch _transformBatch;
		unordered_map<uint, glm::mat4> _transforms;   // entity id -> world matrix

		vector<pair<Handle<Buffer>, vector<uint8_t>>> _pendingTableFills;

		// Material batches (keyed by material name)
		unordered_map<string, MaterialBatch> _materialBatches;

		// Persistent GPU buffers (handle pattern), owned via ResourceManager.
		Handle<Buffer> _instanceBuffer;
		uint _instanceCount = 0;

		IndirectDrawBuffer _indirectDrawBuffer;
		Handle<Buffer> _indirectCommandBuffer;
		Handle<Buffer> _materialIndexBuffer;

		// Object data buffer for GPU Culling (bounding spheres, transform indices)
		Handle<Buffer> _objectDataBuffer;

		bool _hasGpuBuffers = false; // GPU buffers created at least once
		bool _dirty = false;         // scene membership changed since the last rebuild
		uint64_t _revision = 0;      // incremented on every rebuild

		// Screen extents for the Hi-Z pyramid sizing
		VkExtent2D _extents = {};
	};
}
