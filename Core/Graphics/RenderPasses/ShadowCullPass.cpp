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

ShadowCullPass::ShadowCullPass(Device& device, RenderScene& renderScene, ShadowPass& shadowPass)
	: _device(device)
	, _renderScene(renderScene)
	, _shadowPass(shadowPass)
{
	auto& resourceManager = _device.GetResourceManager();

	_cullShader = resourceManager.LoadShader("Shaders/frustumCulling.comp.spv");
	_cullPipeline = resourceManager.LoadComputePipeline("Shaders/frustumCulling.comp.spv");

	_resetShader = resourceManager.LoadShader("Shaders/resetDrawCommandsSimple.comp.spv");
	_resetPipeline = resourceManager.LoadComputePipeline("Shaders/resetDrawCommandsSimple.comp.spv");
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

	// (Re)create the per-cascade draw lists when the draw set changed. All
	// cascades are refreshed together, even inactive ones: the culling shader
	// only rewrites instance counts, so a cascade that activates later must
	// already hold the fill from the current draw set.
	auto& slot = _slots[&frameResources];

	const auto& drawCommands = batch.GetIndirectDrawBuffer().GetDrawCommands();

	BufferDesc indirectDesc{};
	indirectDesc.size = drawCommands.size() * sizeof(DrawIndexedIndirectCommand);
	indirectDesc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
		| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	indirectDesc.memoryType = MemoryType::DEVICE_LOCAL;

	const uint64_t revision = batch.GetRevision();
	const bool rebuild = !slot.buffersCreated || slot.batchRevision != revision;

	array<Handle<Buffer>, SHADOW_MAP_CASCADE_COUNT> handles;
	for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
	{
		handles[i] = rebuild
			? frameResources.CreateOrReplaceStorageBuffer(IndirectName(i), indirectDesc, drawCommands)
			: frameResources.GetOrCreateStorageBuffer(IndirectName(i), indirectDesc);
	}
	slot.batchRevision = revision;
	slot.buffersCreated = true;

	// Only the active cascades are declared, so the shadow pass sees exactly
	// the lists that were culled this frame (HasBuffer fails for the rest).
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		_views[i] = _shadowPass.GetCascadeView(i);

		_indirect[i] = builder.ImportBuffer(IndirectName(i), handles[i]);
		builder.Write(_indirect[i], BufferAccess::StorageComputeWrite);

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

	// Reset every active cascade's instance counts first...
	commandBuffer.BindPipeline(&_resetPipeline.Get());
	auto& resetShader = _resetShader.Get();
	for (uint32_t i = 0; i < _cascadeCount; ++i)
	{
		auto resetBuilder = context.CreateDescriptorSetBuilder(resetShader, 0);
		resetBuilder.SetStorageBuffer(0, context.GetBuffer(_indirect[i]));
		auto& resetResources = resetBuilder.Build();

		commandBuffer.PushConstants(resetShader, 0, drawCount);
		commandBuffer.BindDescriptorSet(_resetPipeline.Get().GetPipelineBindPoint(),
			resetShader, resetResources);
		commandBuffer.Dispatch(std::max(1u, (drawCount + 63) / 64), 1, 1);
	}

	// ...then one same-pass barrier before the culling dispatches read them.
	{
		auto barriers = commandBuffer.CreateBarrierBatch();
		for (uint32_t i = 0; i < _cascadeCount; ++i)
		{
			barriers.Buffer(context.GetBuffer(_indirect[i]),
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		}
		barriers.Submit();
	}

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
		cullBuilder.SetStorageBuffer(2, batch.GetTransformBatch().TransformBuffer.Get());
		cullBuilder.SetStorageBuffer(3, batch.GetInstanceBuffer());
		cullBuilder.SetStorageBuffer(4, context.GetBuffer(_indirect[i]));
		auto& cullResources = cullBuilder.Build();

		commandBuffer.BindDescriptorSet(_cullPipeline.Get().GetPipelineBindPoint(),
			cullShader, cullResources);
		commandBuffer.Dispatch(std::max(1u, (instanceCount + 63) / 64), 1, 1);
	}

	commandBuffer.EndDebugMarker();
}
