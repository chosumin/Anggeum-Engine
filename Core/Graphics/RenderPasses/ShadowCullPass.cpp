#include "stdafx.h"
#include "ShadowCullPass.h"
#include "ShadowPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/ResourceManager.h"
#include "Utils/Math.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

ShadowCullPass::ShadowCullPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene, ShadowPass& shadowPass)
	: _device(device)
	, _renderScene(renderScene)
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
	_cascadeCount = 0;

	PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
	if (!camera)
		return; // declares nothing: the pass culls itself this frame

	auto& batch = renderFrame.GetRendererBatch();
	if (batch.GetDrawCommandCount() == 0)
		return;

	// This pass runs before the shadow pass, so the cascades are computed here.
	_shadowPass.UpdateCascades(camera);
	_cascadeCount = _shadowPass.GetCascadeCount();
	if (_cascadeCount == 0)
		return;

	const uint32_t drawCount = batch.GetDrawCommandCount();

	// The cascade culls scatter instance IDs into the batch-owned buffer;
	// declared so the graph orders it against every ID read and rewrite.
	FGBuffer instanceIDs = builder.HasBuffer(RendererBatch::SB_INSTANCE_IDS)
		? builder.GetBuffer(RendererBatch::SB_INSTANCE_IDS)
		: builder.ImportBuffer(RendererBatch::SB_INSTANCE_IDS,
			batch.GetInstanceBufferHandle());
	builder.Write(instanceIDs, BufferAccess::StorageComputeWrite);

	// Only the active cascades are declared, so the shadow pass sees exactly
	// the lists that were culled this frame (HasBuffer fails for the rest).
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		_views[i] = _shadowPass.GetCascadeView(i);

		_instanceCounts[i] = builder.CreateBuffer(CountsName(i),
			{ drawCount * sizeof(uint32_t),
			  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
		builder.Write(_instanceCounts[i], BufferAccess::StorageComputeWrite);

		_indirect[i] = builder.CreateBuffer(IndirectName(i),
			{ drawCount * sizeof(DrawIndexedIndirectCommand),
			  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
		builder.Write(_indirect[i], BufferAccess::StorageComputeWrite);

		_visibleMaterials[i] = builder.CreateBuffer(MaterialsName(i),
			{ drawCount * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
		builder.Write(_visibleMaterials[i], BufferAccess::StorageComputeWrite);

		_drawCounts[i] = builder.CreateBuffer(DrawCountName(i),
			{ sizeof(uint32_t),
			  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
			  | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
		builder.Write(_drawCounts[i], BufferAccess::StorageComputeWrite);

		_cullData[i] = frameResources.GetOrCreateUniformBuffer<GPUFrustumCullData>(
			"ShadowCull.Cascade" + std::to_string(i) + ".CullData");
	}

	_active = true;
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
	barriers.Submit();

	commandBuffer.BindPipeline(&_cullPipeline.Get());
	auto& cullShader = _cullShader.Get();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		GPUFrustumCullData cullData{};
		cullData.view = _views[i].View;
		cullData.proj = _views[i].Projection;
		cullData.drawCount = instanceCount;

		glm::mat4 viewProj = _views[i].Projection * _views[i].View;
		Math::ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

		Buffer& cullDataBuffer = _cullData[i].Get();
		cullDataBuffer.Update(cullData);

		auto cullBuilder = context.CreateDescriptorSetBuilder(cullShader, 0);
		cullBuilder.SetUniformBuffer(0, cullDataBuffer);
		cullBuilder.SetStorageBuffer(1, batch.GetObjectDataBuffer());
		cullBuilder.SetStorageBuffer(2, batch.GetTransformBuffer());
		cullBuilder.SetStorageBuffer(3, batch.GetInstanceBuffer());
		cullBuilder.SetStorageBuffer(4, batch.GetIndirectCommandBuffer());
		cullBuilder.SetStorageBuffer(6, context.GetBuffer(_instanceCounts[i]));
		auto& cullResources = cullBuilder.Build();

		commandBuffer.BindDescriptorSet(_cullPipeline.Get().GetPipelineBindPoint(),
			cullShader, cullResources);
		commandBuffer.Dispatch(std::max(1u, (instanceCount + 63) / 64), 1, 1);
	}

	// The culls wrote the counts the compaction below reads.
	auto barriers = commandBuffer.CreateBarrierBatch();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		barriers.Buffer(context.GetBuffer(_instanceCounts[i]),
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
	barriers.Submit();

	struct CompactPush
	{
		uint32_t drawCount;
		uint32_t applyBaseCounts;
	};
	CompactPush push{ drawCount, 0 };

	commandBuffer.BindPipeline(&_compactPipeline.Get());
	auto& compactShader = _compactShader.Get();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		auto compactBuilder = context.CreateDescriptorSetBuilder(compactShader, 0);
		compactBuilder.SetStorageBuffer(0, batch.GetIndirectCommandBuffer());
		compactBuilder.SetStorageBuffer(1, batch.GetMaterialIndexBuffer());
		compactBuilder.SetStorageBuffer(2, context.GetBuffer(_instanceCounts[i]));
		compactBuilder.SetStorageBuffer(3, context.GetBuffer(_instanceCounts[i]));
		compactBuilder.SetStorageBuffer(4, context.GetBuffer(_indirect[i]));
		compactBuilder.SetStorageBuffer(5, context.GetBuffer(_visibleMaterials[i]));
		compactBuilder.SetStorageBuffer(6, context.GetBuffer(_drawCounts[i]));
		auto& compactResources = compactBuilder.Build();

		commandBuffer.BindDescriptorSet(_compactPipeline.Get().GetPipelineBindPoint(),
			compactShader, compactResources);
		commandBuffer.PushConstants(compactShader, 0, push);
		commandBuffer.Dispatch(std::max(1u, (drawCount + 63) / 64), 1, 1);
	}

	commandBuffer.EndDebugMarker();
}
