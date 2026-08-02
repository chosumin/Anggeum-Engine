#include "stdafx.h"
#include "RenderFrame.h"
#include "Vulkans/UniformBuffer.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/SubmitInfo.h"
#include "Vulkans/Shader.h"
#include "Vulkans/Pipeline.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "FrustumCuller.h"
#include "RendererBatch.h"
#include "MeshBufferManager.h"
#include "MaterialManager.h"

using namespace Core;

RenderFrame::RenderFrame(Device& device, RenderScene& renderScene)
	: _device(device)
	, _renderScene(renderScene)
	, _resources(device)
{
	CreateSyncObjects();
}

RenderFrame::~RenderFrame()
{
	if (_submission.imageAvailableSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(_device.GetDevice(), _submission.imageAvailableSemaphore, nullptr);
	}
	if (_submission.renderFinishedSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(_device.GetDevice(), _submission.renderFinishedSemaphore, nullptr);
	}
}

void RenderFrame::Reset()
{
	// submitScratch points into submitInfos, so both are cleared together here.
	_submission.submitInfos.clear();
	_submission.submitScratch.clear();

	// Recycles this frame's descriptor pool and clears transient selectors.
	_resources.Reset();
}

DescriptorSetResources* RenderFrame::GetBindlessResources()
{
	if (!HasBindlessSupport())
		return nullptr;

	_bindlessResources.descriptorSet = _renderScene.GetBindlessTextureManager()->GetDescriptorSet();
	_bindlessResources.setIndex = static_cast<uint32_t>(DescriptorSetType::Bindless);
	return &_bindlessResources;
}

void RenderFrame::CreateSyncObjects()
{
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	if (vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_submission.imageAvailableSemaphore) != VK_SUCCESS ||
		vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_submission.renderFinishedSemaphore) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create semaphores for a frame!");
	}
}

SubmitInfo& RenderFrame::AddSubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext)
{
	_submission.submitInfos.emplace_back(queueType, commandBuffer, syncContext);
	return _submission.submitInfos.back();
}

FrustumCuller* RenderFrame::PrepareFrustumCuller(CameraBuffer& camera)
{
	auto& batch = GetRendererBatch();
	if (batch.GetDrawCommandCount() == 0)
		return nullptr;

	return &_resources.GetOrCreateFrustumCuller(batch, camera);
}

void RenderFrame::DrawIndirect(CommandBuffer& commandBuffer,
	Shader& shader, Pipeline& pipeline, Buffer& indirectCommandBuffer,
	DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook)
{
	auto& batch = GetRendererBatch();
	if (batch.GetDrawCommandCount() == 0)
		return;

	auto& meshBufferManager = GetMeshBufferManager();

	auto vertexAttibuteNames = shader.GetVertexAttirbuteNames();

	auto vertexBufferHandles = meshBufferManager.GetVertexBuffers(vertexAttibuteNames);
	vector<Buffer*> vertexBuffers;
	vertexBuffers.reserve(vertexBufferHandles.size());
	for (auto& handle : vertexBufferHandles)
		vertexBuffers.push_back(&handle.Get());

	commandBuffer.BindVertexBuffers(vertexBuffers, 0);
	commandBuffer.BindIndexBuffer(meshBufferManager.GetIndexBuffer().Get(), meshBufferManager.GetIndexType());

	commandBuffer.BindPipeline(&pipeline);

	builder.SetStorageBuffer(1, batch.GetTransformBatch().TransformBuffer.Get());
	builder.SetStorageBuffer(2, batch.GetInstanceBuffer());
	builder.SetUniformBuffer(8, GetMaterialManager().GetMaterialBuffer());
	builder.SetStorageBuffer(9, batch.GetMaterialIndexBuffer());

	// Runs before Build() so the hook can contribute its own descriptor resources.
	if (perShaderHook)
		perShaderHook(shader);

	auto& resources = builder.Build();

	vector<DescriptorSetResources*> resourcesList = { &resources };
	if (shader.UsesBindlessTextures())
	{
		auto* bindlessResources = GetBindlessResources();
		if (bindlessResources)
			resourcesList.push_back(bindlessResources);
	}

	commandBuffer.BindDescriptorSets(pipeline.GetPipelineBindPoint(), shader, resourcesList);

	commandBuffer.DrawIndexedIndirect(
		indirectCommandBuffer,
		batch.GetDrawCommandCount(),
		static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
	);
}
