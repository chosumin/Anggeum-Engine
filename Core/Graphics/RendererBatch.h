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

	struct TransformBatch
	{
		// Persistent (handle pattern), owned via ResourceCache; resolve with .Get().
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
	// are read-only inputs to the per-frame Cullers, so one shared copy is safe (the
	// mutable culling outputs live per-frame in the Culler). Rebuilt from the scene by
	// Prepare(), which RenderScene::Sync calls only when the scene structure changed.
	class RendererBatch
	{
	public:
		RendererBatch(Device& device);
		~RendererBatch();

		// Rebuild the whole draw set from the current scene membership. Self-gated on
		// the scene's draw-set dirty flag; no-op otherwise.
		void Sync(Scene& scene, VkExtent2D extents);

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

		TransformBatch _transformBatch;
		unordered_map<uint, glm::mat4> _transforms;   // entity id -> world matrix

		// Material batches (keyed by material name)
		unordered_map<string, MaterialBatch> _materialBatches;

		// Persistent GPU buffers (handle pattern), owned via ResourceCache.
		Handle<Buffer> _instanceBuffer;
		uint _instanceCount = 0;

		IndirectDrawBuffer _indirectDrawBuffer;
		Handle<Buffer> _indirectCommandBuffer;
		Handle<Buffer> _materialIndexBuffer;

		// Object data buffer for GPU Culling (bounding spheres, transform indices)
		Handle<Buffer> _objectDataBuffer;

		bool _hasGpuBuffers = false; // GPU buffers created at least once

		// Screen extents for Culler initialization
		VkExtent2D _extents = {};
	};
}
