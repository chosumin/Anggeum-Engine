#pragma once

namespace Core
{
	class Material;
	class Pipeline;
	class MeshRenderer;
	class Shader;
	class RenderPass;
	class PipelineState;

	class RendererBatch
	{
	public:
		static void Sort(type_index type);

		RendererBatch(Device& device, Shader& shader, RenderPass& renderPass, PipelineState& pipelineState);
		~RendererBatch();

		void Add(MeshRenderer& meshRenderer);

		Pipeline* Pipeline;
		Shader& SharedShader;
		unordered_map<type_index, Material*> Materials;
		unordered_map<type_index, unordered_map<string, vector<MeshRenderer*>>> MeshBatches;
	};
}

