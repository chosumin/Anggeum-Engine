#pragma once
#include "IndirectDrawBuffer.h"
#include "BufferObjects.h"

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
		Buffer* TransformBuffer;
		vector<uint> EntityIds;
	};

	struct SubMeshBatch
	{
		weak_ptr<SubMesh> SubMesh;
		vector<uint> Transforms;
		uint32_t FirstInstance;
	};

	struct MaterialBatch
	{
		weak_ptr<Material> Material;
		unordered_map<string, SubMeshBatch> SubMeshBatches;
	};

	// RendererBatch: A batch of draw calls for meshes.
	// Contains material batches grouped by material.
	class RendererBatch
	{
	public:
		RendererBatch(Device& device, Scene& scene, TransformBatch& transformBatch, VkExtent2D extents);
		~RendererBatch();

		Buffer* GetObjectDataBuffer() const { return _objectDataBuffer; }
		Buffer* GetIndirectCommandBuffer() const { return _indirectCommandBuffer; }
		Buffer* GetMaterialIndexBuffer() const { return _materialIndexBuffer; }
		uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
		uint32_t GetInstanceCount() const { return _instanceCount; }
		Buffer* GetInstanceBuffer() const { return _instanceBuffer; }
		const IndirectDrawBuffer& GetIndirectDrawBuffer() const { return _indirectDrawBuffer; }
		TransformBatch* GetTransformBatch() const { return _transformBatch; }
		VkExtent2D GetExtents() const { return _extents; }
		shared_ptr<Material> GetFirstMaterial() const;

	private:
		void AddMesh(uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh);
		void PrepareGPUDrivenRendering(VkExtent2D extents);
		void CreateInstanceBuffer(Device& device);

	private:
		Device& _device;
		TransformBatch* _transformBatch = nullptr;

		// Material batches (keyed by material name)
		unordered_map<string, MaterialBatch> _materialBatches;

		Core::Buffer* _instanceBuffer = nullptr;
		uint _instanceCount = 0;

		IndirectDrawBuffer _indirectDrawBuffer;
		Core::Buffer* _indirectCommandBuffer = nullptr;
		Core::Buffer* _materialIndexBuffer = nullptr;

		// Object data buffer for GPU Culling (bounding spheres, transform indices)
		Core::Buffer* _objectDataBuffer = nullptr;

		// Screen extents for Culler initialization
		VkExtent2D _extents = {};
	};
}

