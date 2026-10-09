#include "stdafx.h"
#include "ShadowCullPass.h"
#include "ShadowPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "TerrainNodeListPass.h"
#include "Utils/Math.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

ShadowCullPass::ShadowCullPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
	TerrainPatchCuller& terrainCuller, ShadowPass& shadowPass)
	: _device(device)
	, _renderScene(renderScene)
	, _terrainCuller(terrainCuller)
	, _shadowPass(shadowPass)
{
	_cullShader = resourceManager.LoadShader("Shaders/frustumCulling.comp.spv");
	_cullPipeline = resourceManager.LoadComputePipeline("Shaders/frustumCulling.comp.spv");

	_compactShader = resourceManager.LoadShader("Shaders/compactDrawCommands.comp.spv");
	_compactPipeline = resourceManager.LoadComputePipeline("Shaders/compactDrawCommands.comp.spv");
}

ShadowCullPass::~ShadowCullPass() = default;

void ShadowCullPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	_active = false;
	_meshActive = false;
	_terrainActive = false;
	_cascadeCount = 0;

	PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
	if (!camera)
		return; // declares nothing: the pass culls itself this frame

	// This pass runs before the shadow pass, so the cascades are computed here.
	_shadowPass.UpdateCascades(camera);
	_cascadeCount = _shadowPass.GetCascadeCount();
	if (_cascadeCount == 0)
		return;

	for (uint32_t i = 0; i < _cascadeCount; ++i)
		_views[i] = _shadowPass.GetCascadeView(i);

	// Only the active cascades are declared, so the shadow pass sees exactly
	// the lists that were culled this frame (HasBuffer fails for the rest).
	auto& batch = renderFrame.GetRendererBatch();
	_meshActive = batch.GetDrawCommandCount() > 0;

	// Change only when the tables grow, so transients sized by them are not
	// rebuilt on every load or unload.
	const uint32_t drawCapacity = batch.GetDrawCommandCapacity();
	const uint32_t instanceCapacity = batch.GetInstanceCapacity();

	for (uint32_t i = 0; _meshActive && i < _cascadeCount; ++i)
	{
		_instanceCounts[i] = builder.CreateBuffer(CountsName(i),
			{ drawCapacity * sizeof(uint32_t),
			  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
		builder.Write(_instanceCounts[i], BufferAccess::FillComputeWrite);

		// Each cascade scatters into its own ID buffer, so its draw never
		// reads another cascade's IDs.
		_instanceIDs[i] = builder.CreateBuffer(InstanceIdsName(i),
			{ instanceCapacity * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
		builder.Write(_instanceIDs[i], BufferAccess::StorageComputeWrite);

		_indirect[i] = builder.CreateBuffer(IndirectName(i),
			{ drawCapacity * sizeof(DrawIndexedIndirectCommand),
			  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
		builder.Write(_indirect[i], BufferAccess::StorageComputeWrite);

		_drawCounts[i] = builder.CreateBuffer(DrawCountName(i),
			{ sizeof(uint32_t),
			  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
			  | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
		builder.Write(_drawCounts[i], BufferAccess::FillComputeWrite);

		_cullData[i] = frameResources.GetOrCreateUniformBuffer<GPUCullData>(
			"ShadowCull.Cascade" + std::to_string(i) + ".CullData");
	}

	// Terrain: the same node list the camera culls, against each cascade's
	// frustum (no Hi-Z); the LOD map keeps the stitching identical to the
	// main draw, so shadows match the rendered surface.
	_terrainActive = builder.HasBuffer(TerrainNodeListPass::SB_NODE_LIST);
	if (_terrainActive)
	{
		const TerrainConfig& config = _renderScene.GetTerrainSystem().GetConfig();
		_terrainInputs = _terrainCuller.SetupInputs(builder, frameResources);
		_terrainPush = _terrainCuller.BuildPush(*camera);

		for (uint32_t i = 0; i < _cascadeCount; ++i)
		{
			_terrainOutputs[i].patchList = _terrainCuller.CreatePatchList(builder, TerrainPatchListName(i));
			_terrainOutputs[i].drawArgs = _terrainCuller.CreateDrawArgs(builder, TerrainDrawArgsName(i));

			TerrainCullData cullData{};
			cullData.view = _views[i].View;
			cullData.proj = _views[i].Projection;
			Math::ExtractFrustumPlanes(cullData.proj * cullData.view, cullData.frustumPlanes);
			cullData.screenHiZ = vec4(0.0f, 0.0f, 1.0f, 0.0f);
			cullData.heightBounds = vec4(config.heightMin, config.heightMax, 2.0f, 0.0f);

			_terrainCullData[i] = frameResources.GetOrCreateUniformBuffer<TerrainCullData>(
				"ShadowCull.Cascade" + std::to_string(i) + ".TerrainCullData");
			_terrainCullData[i].Get().Update(cullData);
		}
	}

	_active = _meshActive || _terrainActive;
}

void ShadowCullPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	auto& batch = context.GetRenderFrame().GetRendererBatch();
	const uint32_t drawCount = batch.GetDrawCommandCount();
	const uint32_t instanceCount = batch.GetInstanceCount();

	commandBuffer.BeginDebugMarker("Shadow Cascade Culling");

	auto barriers = commandBuffer.CreateBarrierBatch();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		if (_meshActive)
		{
			commandBuffer.FillBuffer(context.GetBuffer(_instanceCounts[i]), 0, VK_WHOLE_SIZE, 0);
			commandBuffer.FillBuffer(context.GetBuffer(_drawCounts[i]), 0, VK_WHOLE_SIZE, 0);

			barriers.Buffer(context.GetBuffer(_instanceCounts[i]),
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
			barriers.Buffer(context.GetBuffer(_drawCounts[i]),
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		}

		if (_terrainActive)
		{
			Buffer& terrainArgs = context.GetBuffer(_terrainOutputs[i].drawArgs);
			_terrainCuller.ResetDrawArgs(commandBuffer, terrainArgs);
			barriers.Buffer(terrainArgs,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		}
	}
	barriers.Submit();

	if (_terrainActive)
	{
		for (uint32_t i = 0; i < _cascadeCount; ++i)
			_terrainCuller.Dispatch(context, commandBuffer, _terrainInputs, _terrainOutputs[i],
				_terrainCullData[i].Get(), nullptr, _terrainPush);
	}

	if (!_meshActive)
	{
		commandBuffer.EndDebugMarker();
		return;
	}

	commandBuffer.BindPipeline(&_cullPipeline.Get());
	auto& cullShader = _cullShader.Get();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		// Shared cull block; the Hi-Z fields stay zero (frustum only).
		GPUCullData cullData{};
		cullData.view = _views[i].View;
		cullData.proj = _views[i].Projection;
		cullData.drawCount = instanceCount;

		glm::mat4 viewProj = _views[i].Projection * _views[i].View;
		Math::ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

		Buffer& cullDataBuffer = _cullData[i].Get();
		cullDataBuffer.Update(cullData);

		auto cullBuilder = context.CreateDescriptorSetBuilder(cullShader, 0);
		cullBuilder.SetUniformBuffer(0, cullDataBuffer);
		cullBuilder.SetStorageBuffer(1, batch.GetInstanceDataBuffer());
		cullBuilder.SetStorageBuffer(2, batch.GetTransformBuffer());
		cullBuilder.SetStorageBuffer(3, context.GetBuffer(_instanceIDs[i]));
		cullBuilder.SetStorageBuffer(4, batch.GetIndirectCommandBuffer());
		cullBuilder.SetStorageBuffer(6, context.GetBuffer(_instanceCounts[i]));
		auto& cullResources = cullBuilder.Build();

		commandBuffer.BindDescriptorSet(_cullPipeline.Get().GetPipelineBindPoint(),
			cullShader, cullResources);
		commandBuffer.Dispatch(std::max(1u, (instanceCount + 63) / 64), 1, 1);
	}

	// The culls wrote the counts the compaction below reads.
	auto barrier = commandBuffer.CreateBarrierBatch();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		barrier.Buffer(context.GetBuffer(_instanceCounts[i]),
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
	barrier.Submit();

	commandBuffer.BindPipeline(&_compactPipeline.Get());
	auto& compactShader = _compactShader.Get();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		auto compactBuilder = context.CreateDescriptorSetBuilder(compactShader, 0);
		compactBuilder.SetStorageBuffer(0, batch.GetIndirectCommandBuffer());
		compactBuilder.SetStorageBuffer(1, context.GetBuffer(_instanceCounts[i]));
		compactBuilder.SetStorageBuffer(2, context.GetBuffer(_indirect[i]));
		compactBuilder.SetStorageBuffer(3, context.GetBuffer(_drawCounts[i]));
		auto& compactResources = compactBuilder.Build();

		commandBuffer.BindDescriptorSet(_compactPipeline.Get().GetPipelineBindPoint(),
			compactShader, compactResources);
		commandBuffer.PushConstants(compactShader, 0, drawCount);
		commandBuffer.Dispatch(std::max(1u, (drawCount + 63) / 64), 1, 1);
	}

	commandBuffer.EndDebugMarker();
}
