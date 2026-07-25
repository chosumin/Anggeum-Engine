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
	class RenderFrame;
	class FrameResources;
	class Buffer;
	class Culler;
	class Scene;

	struct TransformBatch
	{
		// Pool-owned by FrameResources (handle pattern); resolve with .Get().
		Handle<Buffer> TransformBuffer;
		vector<uint> EntityIds;
	};

	struct SubMeshBatch
	{
		Handle<SubMesh> SubMesh;
		vector<uint> Transforms;
		uint32_t FirstInstance;
	};

	struct MaterialBatch
	{
		Handle<Material> Material;
		unordered_map<string, SubMeshBatch> SubMeshBatches;
	};

	// RendererBatch: A batch of draw calls for meshes.
	// Contains material batches grouped by material.
	class RendererBatch
	{
	public:
		RendererBatch(Device& device, Scene& scene, TransformBatch& transformBatch,
			RenderFrame& renderFrame, VkExtent2D extents);
		~RendererBatch();

		Buffer& GetObjectDataBuffer() const { return _objectDataBuffer.Get(); }
		Buffer& GetIndirectCommandBuffer() const { return _indirectCommandBuffer.Get(); }
		Buffer& GetMaterialIndexBuffer() const { return _materialIndexBuffer.Get(); }
		uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
		uint32_t GetInstanceCount() const { return _instanceCount; }
		Buffer& GetInstanceBuffer() const { return _instanceBuffer.Get(); }
		const IndirectDrawBuffer& GetIndirectDrawBuffer() const { return _indirectDrawBuffer; }
		TransformBatch& GetTransformBatch() const { return _transformBatch; }
		VkExtent2D GetExtents() const { return _extents; }

	private:
		void AddMesh(uint entityId, Handle<Material> material, Handle<SubMesh> subMesh);
		void PrepareGPUDrivenRendering(FrameResources& frameResources, VkExtent2D extents);
		void CreateInstanceBuffer(FrameResources& frameResources);

	private:
		Device& _device;
		// Owned by RenderExecutor, which outlives this batch.
		TransformBatch& _transformBatch;

		// Material batches (keyed by material name)
		unordered_map<string, MaterialBatch> _materialBatches;

		// Buffers are pool-owned by FrameResources (handle pattern); held by handle.
		Handle<Buffer> _instanceBuffer;
		uint _instanceCount = 0;

		IndirectDrawBuffer _indirectDrawBuffer;
		Handle<Buffer> _indirectCommandBuffer;
		Handle<Buffer> _materialIndexBuffer;

		// Object data buffer for GPU Culling (bounding spheres, transform indices)
		Handle<Buffer> _objectDataBuffer;

		// Screen extents for Culler initialization
		VkExtent2D _extents = {};
	};
}

