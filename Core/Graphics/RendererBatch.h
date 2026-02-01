#pragma once
#include "IndirectDrawBuffer.h"

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
	class RenderFrame;
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
		RendererBatches(TransformBatch& transformBatch);
		~RendererBatches();

		void Prepare(Device& device, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes);
		void PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes);
		void PrepareIndirectCommands(Device& device);

		void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw);

		void DrawIndirect(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>)> perDraw);
	private:
		//Add batch depending on the mesh's materials.
		void AddBatch(Device& device, RenderPass& renderPass, PipelineState& pipelineState, uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh);
		void CreateInstanceBuffer(Device& device);
	private:
		unordered_map<uint32_t, ShaderBatch> _shaderBatches;
		TransformBatch& _transformBatch;
		Core::Buffer* _instanceBuffer;
		uint _instanceCount;

		IndirectDrawBuffer _indirectDrawBuffer;
		Core::Buffer* _indirectCommandBuffer;
		Core::Buffer* _materialIndexBuffer;

		static constexpr uint32_t MAX_DRAW_COMMANDS = 10000;
	};
}

