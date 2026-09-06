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

	// RendererBatch: the application-wide GPU-driven draw set, shared by every
	// frame-in-flight (culling reads it; mutable outputs are per-frame graph
	// buffers). Tables are sized for the whole scene membership; resident
	// geometry fills a dense prefix in promotion order, and each promotion
	// appends its own entries via offset copies instead of a rebuild.
	class RendererBatch
	{
	public:
		RendererBatch(Device& device, ResourceManager& resourceManager);
		~RendererBatch();

		void MarkDirty() { _dirty = true; }

		// Bumped on every table change so holders sized against the notice they went stale.
		uint64_t GetRevision() const { return _revision; }

		// Membership rebuild when dirty, then appends newly-resident geometry.
		void Sync(Scene& scene);

		void QueuePendingInit(FrameResources& frameResources);

		Buffer& GetObjectDataBuffer() const { return _objectDataBuffer.Get(); }
		Buffer& GetIndirectCommandBuffer() const { return _indirectCommandBuffer.Get(); }
		Buffer& GetMaterialIndexBuffer() const { return _materialIndexBuffer.Get(); }
		uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
		uint32_t GetInstanceCount() const { return _instanceCount; }
		Buffer& GetInstanceBuffer() const { return _instanceBuffer.Get(); }
		const IndirectDrawBuffer& GetIndirectDrawBuffer() const { return _indirectDrawBuffer; }
		Buffer& GetTransformBuffer() const { return _transformBuffer.Get(); }

	private:
		// A submesh waiting for residency; appended once its upload lands.
		struct PendingDraw
		{
			Handle<Material> Material;
			Handle<SubMesh> SubMesh;
			vector<uint> Transforms;
		};

		struct TableFill
		{
			Handle<Buffer> Buffer;
			vector<uint8_t> Bytes;
			VkDeviceSize Offset = 0;
		};

		void AddMesh(uint entityId, Handle<Material> material, Handle<SubMesh> subMesh);
		void InitializeFromScene(Scene& scene);
		void RebuildGpuBuffers();

		// Appends one submesh to the mirror prefix; returns its command slot.
		uint32_t AppendDraw(Handle<Material> material, Handle<SubMesh> subMesh,
			const vector<uint>& transforms);
		void AppendPendingResident();

		// Create the buffer on first use; replace only when the size changed.
		Handle<Buffer> AcquirePersistentBuffer(Handle<Buffer> current,
			const struct BufferDesc& desc, const string& name);

	private:
		Device& _device;
		ResourceManager& _resourceManager;

		vector<TableFill> _pendingTableFills;

		// Not yet resident at the last rebuild, keyed by submesh name.
		unordered_map<string, PendingDraw> _pendingDraws;

		Handle<Buffer> _transformBuffer;
		unordered_map<uint, glm::mat4> _transforms;   // entity id -> world matrix

		// Material batches (keyed by material name)
		unordered_map<string, MaterialBatch> _materialBatches;

		Handle<Buffer> _instanceBuffer;
		uint _instanceCount = 0;          // resident instances (live prefix)
		uint32_t _instanceCapacity = 0;   // total membership instances
		uint32_t _commandCapacity = 0;    // total membership submeshes

		IndirectDrawBuffer _indirectDrawBuffer;
		Handle<Buffer> _indirectCommandBuffer;
		Handle<Buffer> _materialIndexBuffer;

		// Object data buffer for GPU Culling (bounding spheres, transform indices)
		Handle<Buffer> _objectDataBuffer;
		vector<GPUObjectData> _objectData;

		bool _hasGpuBuffers = false; // GPU buffers created at least once
		bool _dirty = false;         // scene membership changed since the last rebuild
		uint64_t _revision = 0;
	};
}
