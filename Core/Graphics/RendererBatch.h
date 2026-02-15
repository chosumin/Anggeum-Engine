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
		void PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes);
		void PrepareGPUDrivenRendering(Device& device, bool needMaterialData,
			VkExtent2D extents);

		void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw);

		void DrawIndirect(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>)> perDraw);
		void DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer, const CameraBuffer& camera);

		void SetPreviousDepthBuffer(shared_ptr<Texture> depthBuffer) { _previousDepthBuffer = depthBuffer; }
	private:
		void AddBatch(Device& device, RenderPass& renderPass, PipelineState& pipelineState, uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh);
		void CreateInstanceBuffer(Device& device);

		void PrepareCullingResources(Core::Device& device);
		void ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes);

		// Hi-Z Occlusion Culling
		void PrepareHiZResources(Device& device, VkExtent2D extents);
		void GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer);
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

		// Buffers for GPU Culling
		Core::Buffer* _objectDataBuffer = nullptr;
		Core::Buffer* _visibleCountsBuffer = nullptr;
		shared_ptr<Shader> _cullingShader;
		unique_ptr<Pipeline> _cullingPipeline;

		// Hi-Z Resources
		shared_ptr<Texture> _hiZTexture;
		shared_ptr<Shader> _hiZGenerateShader = nullptr;
		unique_ptr<Pipeline> _hiZPipeline;
		shared_ptr<Texture> _previousDepthBuffer = nullptr;
		uint32_t _hiZMipLevels = 0;
		VkExtent2D _screenExtent = {};

		shared_ptr<Shader> _depthResolveShader = nullptr;
		unique_ptr<Pipeline> _depthResolvePipeline;

		bool _hiZInitialized = false;
	};
}

