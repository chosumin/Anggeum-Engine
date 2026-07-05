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

	struct ShaderBatch
	{
		Pipeline* Pipeline;
		weak_ptr<Shader> SharedShader;
		unordered_map<string, MaterialBatch> MaterialBatches;
	};

	class RendererBatches
	{
	public:
		RendererBatches(Device& device, TransformBatch& transformBatch);
		~RendererBatches();

		void Prepare(Device& device, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes);
		void Prepare(Device& device, const string& shaderName, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes, vector<shared_ptr<Material>>& outMaterials);
		void PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes);
		void PrepareGPUDrivenRendering(Device& device, bool needMaterialData,
			VkExtent2D extents);

		void GpuDrivenDraw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			CameraBuffer& camera,
			Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
			Framebuffer& framebuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>)> perDraw,
			function<void()> postDraw);

		void DrawIndirect(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>)> perDraw);
		void DrawIndirect(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			DescriptorSetBuilder& builder,
			function<void(shared_ptr<Material>)> perDraw);

		void DispatchFrustumOnlyCulling(RenderFrame& renderFrame, 
			CommandBuffer& commandBuffer,
			const CameraBuffer& camera);

		Buffer* GetObjectDataBuffer() const { return _objectDataBuffer; }
			Buffer* GetIndirectCommandBuffer() const { return _indirectCommandBuffer; }
			uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
			uint32_t GetInstanceCount() const { return _instanceCount; }
			Buffer* GetInstanceBuffer() const { return _instanceBuffer; }
			const IndirectDrawBuffer& GetIndirectDrawBuffer() const { return _indirectDrawBuffer; }
			TransformBatch& GetTransformBatch() const { return _transformBatch; }
			VkExtent2D GetExtents() const { return _extents; }
	private:
		void AddBatch(Device& device, RenderPass& renderPass, PipelineState& pipelineState, uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh);
		void CreateInstanceBuffer(Device& device);

		void DrawIndirectInternal(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			Core::Buffer& indirectCommandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>)> perDraw);
	private:
		Device& _device;
		unordered_map<uint32_t, ShaderBatch> _shaderBatches;
		TransformBatch& _transformBatch;
		Core::Buffer* _instanceBuffer;
		uint _instanceCount;

		IndirectDrawBuffer _indirectDrawBuffer;
		Core::Buffer* _indirectCommandBuffer;
		Core::Buffer* _materialIndexBuffer;
		bool _needsMaterialIndexBuffer = false;

		// Object data buffer for GPU Culling (bounding spheres, transform indices)
		Core::Buffer* _objectDataBuffer = nullptr;

		// Screen extents for Culler initialization
		VkExtent2D _extents = {};
	};
}

