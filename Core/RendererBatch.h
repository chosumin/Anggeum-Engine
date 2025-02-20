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
	class RendererBatch
	{
	public:
		static void Sort();

		RendererBatch(Device& device, Shader& shader, RenderPass& renderPass, PipelineState& pipelineState);
		~RendererBatch();

		//Add batch depending on the mesh's materials.
		void Add(Mesh& mesh);

		//Add batch depending on the parameter material.
		void Add(Mesh& mesh, Material& material);

		void Draw(CommandBuffer& commandBuffer,uint32_t currentFrame);

		Pipeline* Pipeline;
		Shader& SharedShader;
		unordered_map<uint32_t, Material*> Materials;

		//uint32_t: material hash, string: sub mesh name
		unordered_map<uint32_t, unordered_map<string, SubMesh*>> SubMeshBatches;

		//key: sub mesh name
		unordered_map<string, vector<Transform*>> Transforms;
	};
}

