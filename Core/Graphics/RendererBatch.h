#pragma once
#include "IndirectDrawBuffer.h"
#include "BufferObjects.h"
#include "ResourceHandle.h"

namespace Core
{
	class Material;
	class Pipeline;
	class Texture;
	class SubMesh;
	class Mesh;
	class Shader;
	class RenderPass;
	class PipelineState;
	class Transform;
	class CommandBuffer;
	class RenderFrame;
	class Buffer;
	class Framebuffer;
	class DescriptorSetBuilder;
	class Culler;
	class Scene;

	struct TransformBatch
	{
		unique_ptr<Buffer> TransformBuffer;
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
		RendererBatch(Device& device, Scene& scene, TransformBatch& transformBatch, VkExtent2D extents);
		~RendererBatch();

		Buffer& GetObjectDataBuffer() const { return *_objectDataBuffer; }
		Buffer& GetIndirectCommandBuffer() const { return *_indirectCommandBuffer; }
		Buffer& GetMaterialIndexBuffer() const { return *_materialIndexBuffer; }
		uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
		uint32_t GetInstanceCount() const { return _instanceCount; }
		Buffer& GetInstanceBuffer() const { return *_instanceBuffer; }
		const IndirectDrawBuffer& GetIndirectDrawBuffer() const { return _indirectDrawBuffer; }
		TransformBatch& GetTransformBatch() const { return _transformBatch; }
		VkExtent2D GetExtents() const { return _extents; }

	private:
		void AddMesh(uint entityId, Handle<Material> material, Handle<SubMesh> subMesh);
		void PrepareGPUDrivenRendering(VkExtent2D extents);
		void CreateInstanceBuffer(Device& device);

	private:
		Device& _device;
		// Owned by RenderExecutor, which outlives this batch.
		TransformBatch& _transformBatch;

		// Material batches (keyed by material name)
		unordered_map<string, MaterialBatch> _materialBatches;

		unique_ptr<Core::Buffer> _instanceBuffer;
		uint _instanceCount = 0;

		IndirectDrawBuffer _indirectDrawBuffer;
		unique_ptr<Core::Buffer> _indirectCommandBuffer;
		unique_ptr<Core::Buffer> _materialIndexBuffer;

		// Object data buffer for GPU Culling (bounding spheres, transform indices)
		unique_ptr<Core::Buffer> _objectDataBuffer;

		// Screen extents for Culler initialization
		VkExtent2D _extents = {};
	};
}

