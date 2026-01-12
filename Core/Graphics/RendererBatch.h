#pragma once

namespace Core
{
	class Material;
	class Pipeline;
	class SubMesh;
	class Mesh;
	class Shader;
	class RenderPass;
	class PipelineState;
	class Transform;
	class CommandBuffer;
	class Buffer;

	struct TransformBatch
	{
		Buffer* TransformBuffer;
		vector<uint> EntityIds;
	};

	struct SubMeshBatch
	{
		weak_ptr<SubMesh> SubMesh;
		vector<uint> Transforms;
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
		RendererBatches(TransformBatch& transformBatch);
		~RendererBatches();

		void Prepare(Device& device, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes);
		void PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes);

		void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame,
			function<void(shared_ptr<Material>)> setMaterial,
			function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> loop);
	private:
		//Add batch depending on the mesh's materials.
		void AddBatch(Device& device, RenderPass& renderPass, PipelineState& pipelineState, uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh);
		void CreateInstanceBuffer(Device& device);
	private:
		unordered_map<uint32_t, ShaderBatch> _shaderBatches;
		TransformBatch& _transformBatch;
		Core::Buffer* _instanceBuffer;
		uint _instanceCount;
	};
}

