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
	class Scene;
	class Buffer;

	class RendererBatch
	{
	public:
		static void Sort();

		RendererBatch(Device& device, Shader& shader, RenderPass& renderPass, PipelineState& pipelineState);
		~RendererBatch();

		//Add batch depending on the mesh's materials.
		void Add(Mesh& mesh);

		//Add batch depending on the parameter material.
		void Add(Mesh& mesh, weak_ptr<Material> material);

		void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, 
			function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> loop);

		Pipeline* Pipeline;
		Shader& SharedShader;
		unordered_map<string, weak_ptr<Material>> Materials;

		//uint32_t: material name, string: sub mesh name
		unordered_map<string, unordered_map<string, weak_ptr<SubMesh>>> SubMeshBatches;

		//key: sub mesh name
		unordered_map<string, vector<uint>> Transforms;
	};

	class RendererBatches
	{
	public:
		RendererBatches();
		~RendererBatches();

		void Prepare(Device& device, RenderPass& renderPass, PipelineState& pipelineState, Scene& scene);
		void PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, Scene& scene);

		void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame,
			function<void(shared_ptr<Material>)> setMaterial,
			function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> loop);
	private:
		unordered_map<uint32_t, RendererBatch*> _batches;
		Core::Buffer* _instanceBuffer;
	};
}

