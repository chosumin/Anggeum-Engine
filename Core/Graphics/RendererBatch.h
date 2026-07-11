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

		void OcclusionCullAndDraw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			Shader& shader, Pipeline& pipeline,
			CameraBuffer& camera,
			Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
			Framebuffer& framebuffer,
			function<void(Shader&)> perShader,
			function<void(shared_ptr<Material>)> perDraw,
			function<void()> postDraw);

		void DrawIndirect(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			Shader& shader,
			DescriptorSetBuilder& builder,
			const CameraBuffer& camera,
			function<void(shared_ptr<Material>)> perDraw);

		void DispatchFrustumOnlyCulling(RenderFrame& renderFrame, 
			CommandBuffer& commandBuffer,
			const CameraBuffer& camera);

		// Combined frustum culling + indirect draw for shadow passes and similar use cases.
		// Dispatches frustum-only culling, then draws with the provided builder and pipeline.
		void FrustumCullAndDraw(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			Core::RenderPass& renderPass,
			Framebuffer& framebuffer,
			Shader& shader,
			Pipeline& pipeline,
			DescriptorSetBuilder& builder,
			const CameraBuffer& camera,
			function<void(shared_ptr<Material>)> perDraw);

		Buffer* GetObjectDataBuffer() const { return _objectDataBuffer; }
		Buffer* GetIndirectCommandBuffer() const { return _indirectCommandBuffer; }
		uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
		uint32_t GetInstanceCount() const { return _instanceCount; }
		Buffer* GetInstanceBuffer() const { return _instanceBuffer; }
		const IndirectDrawBuffer& GetIndirectDrawBuffer() const { return _indirectDrawBuffer; }
		TransformBatch* GetTransformBatch() const { return _transformBatch; }
		VkExtent2D GetExtents() const { return _extents; }

	private:
		void AddMesh(uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh);
		void Finalize();
		void PrepareGPUDrivenRendering(VkExtent2D extents);
		void CreateInstanceBuffer(Device& device);

		void DrawIndirectInternal(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			Shader& shader, Pipeline& pipeline,
			Core::Buffer& indirectCommandBuffer,
			function<void(Shader&)> perShader,
			function<void(shared_ptr<Material>)> perDraw);

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

