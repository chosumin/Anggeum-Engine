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

		void GpuDrivenDraw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			shared_ptr<Texture> prevDepth, shared_ptr<Texture> curDepth,
			CameraBuffer& camera,
			Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
			Framebuffer& framebuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>)> perDraw,
			function<void()> postDraw);
		void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw);
		void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			DescriptorSetBuilder& builder,
			function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw);
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

		// GPU buffer accessors for SDF generation
		Buffer* GetObjectDataBuffer() const { return _objectDataBuffer; }
		Buffer* GetIndirectCommandBuffer() const { return _indirectCommandBuffer; }
		uint32_t GetDrawCommandCount() const { return _indirectDrawBuffer.GetDrawCount(); }
		uint32_t GetInstanceCount() const { return _instanceCount; }

	private:
		void AddBatch(Device& device, RenderPass& renderPass, PipelineState& pipelineState, uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh);
		void CreateInstanceBuffer(Device& device);

		void PrepareCullingResources(Core::Device& device);
		void ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes);

		void DrawIndirectInternal(
			RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			Core::Buffer& indirectCommandBuffer,
			function<void(shared_ptr<Shader>)> perShader,
			function<void(shared_ptr<Material>)> perDraw);

		void PrepareHiZResources(Device& device, VkExtent2D extents);
		void GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer, shared_ptr<Texture> depth);

		void ResetDrawCommands(RenderFrame& renderFrame, CommandBuffer& commandBuffer);
		void DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			const CameraBuffer& camera, shared_ptr<Texture> depth,
			Core::Buffer* indirectCommandBuffer,
			shared_ptr<Shader> cullingShader, Pipeline* cullingPipeline);
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
		shared_ptr<Shader> _cullingShader;
		unique_ptr<Pipeline> _cullingPipeline;

		// Hi-Z Resources
		shared_ptr<Texture> _hiZTexture;
		shared_ptr<Shader> _hiZGenerateShader = nullptr;
		unique_ptr<Pipeline> _hiZPipeline;
		uint32_t _hiZMipLevels = 0;
		VkExtent2D _screenExtent = {};

		shared_ptr<Shader> _depthResolveShader = nullptr;
		unique_ptr<Pipeline> _depthResolvePipeline;

		bool _hiZInitialized = false;

		// 2-Pass Resources
		Core::Buffer* _rejectedIndicesBuffer = nullptr;
		Core::Buffer* _rejectedCountBuffer = nullptr;
		Core::Buffer* _pass2IndirectCommandBuffer = nullptr;

		shared_ptr<Shader> _pass2CullingShader;
		unique_ptr<Pipeline> _pass2CullingPipeline;
		shared_ptr<Shader> _resetDrawCommandsShader;
		unique_ptr<Pipeline> _resetDrawCommandsPipeline;

		// Frustum-only culling resources
		shared_ptr<Shader> _frustumCullingShader;
		unique_ptr<Pipeline> _frustumCullingPipeline;
		shared_ptr<Shader> _resetDrawCommandsSimpleShader;
		unique_ptr<Pipeline> _resetDrawCommandsSimplePipeline;
	};
}

