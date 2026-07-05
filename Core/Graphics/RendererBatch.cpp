#include "stdafx.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Culler.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Foundation/Entity.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"
#include "Vulkans/Texture.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "TransferJob.h"
#include "RendererPasses/ResolvePass.h"

using namespace Core;

Core::RendererBatches::RendererBatches(Device& device, TransformBatch& transformBatch)
	: _device(device)
	, _transformBatch(transformBatch)
	, _instanceBuffer(VK_NULL_HANDLE), _instanceCount(0)
{
}

Core::RendererBatches::~RendererBatches()
{
	if (_materialIndexBuffer != VK_NULL_HANDLE) delete(_materialIndexBuffer);
	if (_indirectCommandBuffer != VK_NULL_HANDLE) delete(_indirectCommandBuffer);
	if (_instanceBuffer != VK_NULL_HANDLE) delete(_instanceBuffer);
	if (_objectDataBuffer != VK_NULL_HANDLE) delete(_objectDataBuffer);

	for (auto& batch : _shaderBatches)
	{
		delete(batch.second.Pipeline);
	}
	_shaderBatches.clear();
}

void Core::RendererBatches::Prepare(Device& device, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes)
{
	for (auto&& mesh : meshes)
	{
		uint entityId = mesh->GetEntity().GetId();

		auto materials = mesh->GetMaterials();
		for (size_t i = 0; i < materials.size(); ++i)
		{
			// Skip materials that don't have the "Geometry" pass
			if (materials[i]->GetShader().GetPass() != "Geometry")
				continue;

			auto& material = materials[i];
			auto& subMesh = mesh->GetSubMeshes()[i];

			AddBatch(device, renderPass, pipelineState, entityId, material, subMesh);
		}
	}

	CreateInstanceBuffer(device);
}

void Core::RendererBatches::PrepareGPUDrivenRendering(Device& device, bool needMaterialData,
	VkExtent2D extents)
{
	_needsMaterialIndexBuffer = needMaterialData;

	_indirectDrawBuffer.Clear();
	uint32_t globalFirstInstance = 0;
	uint32_t drawCommandIndex = 0;

	vector<GPUObjectData> objectData;
	objectData.reserve(_instanceCount);

	for (auto& [shaderHash, shaderBatch] : _shaderBatches)
	{
		for (auto& [materialName, materialBatch] : shaderBatch.MaterialBatches)
		{
			auto material = materialBatch.Material.lock();
			if (!material || !material->HasMaterialIndex())
				continue;

			uint32_t materialIndex = material->GetMaterialIndex();

			for (auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
			{
				auto subMesh = subMeshBatch.SubMesh.lock();
				if (!subMesh || !subMesh->HasAllocation())
					continue;

				const auto& allocation = subMesh->GetAllocation();
				uint32_t instanceCount = static_cast<uint32_t>(subMeshBatch.Transforms.size());

				_indirectDrawBuffer.AddDrawCommand(
					allocation,
					materialIndex,
					_needsMaterialIndexBuffer ? 0 : instanceCount,
					globalFirstInstance
				);

				// Prepare object data for each instance
				for (size_t i = 0; i < subMeshBatch.Transforms.size(); ++i)
				{
					GPUObjectData data{};
					data.boundingSphere = glm::vec4(
						allocation.boundingSphereCenter,
						allocation.boundingSphereRadius);
					data.transformIndex = subMeshBatch.Transforms[i];
					data.drawCommandIndex = drawCommandIndex;

					objectData.push_back(data);
				}

				globalFirstInstance += instanceCount;
				drawCommandIndex++;
			}
		}
	}

	vector<Job*> jobs;

	// Indirect Command Buffer
	Core::VkBufferJob<DrawIndexedIndirectCommand> job(device,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		&_indirectCommandBuffer,
		_indirectDrawBuffer.GetDrawCommands(), 0);
	jobs.push_back(&job);

	// Material Index SSBO
	Core::VkBufferJob<uint32_t> job2(device,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		&_materialIndexBuffer,
		_indirectDrawBuffer.GetMaterialIndices(), 0);
	jobs.push_back(&job2);

	// Object Data SSBO
	Core::VkBufferJob<GPUObjectData> job3(device,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		&_objectDataBuffer,
		objectData, 0);
	jobs.push_back(&job3);

	Core::CommandBuffer::ImmediateSubmit(device, jobs);

	_culler = make_unique<Culler>(device, _transformBatch);
	_culler->Prepare(device, extents,
		_objectDataBuffer, _instanceBuffer, _indirectCommandBuffer,
		_instanceCount, _indirectDrawBuffer);
}

void Core::RendererBatches::PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes)
{
	for (auto&& mesh : meshes)
	{
		uint entityId = mesh->GetEntity().GetId();
		auto& subMeshes = mesh->GetSubMeshes();

		for (auto& subMesh : subMeshes)
		{
			AddBatch(device, renderPass, pipelineState, entityId, material, subMesh);
		}
	}

	CreateInstanceBuffer(device);
}

void Core::RendererBatches::Prepare(Device& device, const string& shaderName, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes, vector<shared_ptr<Material>>& outMaterials)
{
	for (auto&& mesh : meshes)
	{
		uint entityId = mesh->GetEntity().GetId();
		auto& materials = mesh->GetMaterials();
		auto& subMeshes = mesh->GetSubMeshes();

		for (size_t i = 0; i < materials.size(); ++i)
		{
			if (materials[i]->GetShader().GetPass() != "Geometry")
				continue;

			if (i >= subMeshes.size())
				break;

			auto overrideMaterial = device.GetResourceCache()
				.RequestOverrideMaterial(materials[i], shaderName);

			outMaterials.push_back(overrideMaterial);

			AddBatch(device, renderPass, pipelineState, entityId,
				overrideMaterial, subMeshes[i]);
		}
	}

	CreateInstanceBuffer(device);
}

void Core::RendererBatches::GpuDrivenDraw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, 
	CameraBuffer& camera,
	Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
	Framebuffer& framebuffer,
	function<void(shared_ptr<Shader>)> perShader, function<void(shared_ptr<Material>)> perDraw,
	function<void()> postDraw)
{
	auto prevDepth = renderFrame.GetPreviousDepthBuffer();
	auto curDepth = renderFrame.GetRenderTarget(ResolvePass::RT_RESOLVED_DEPTH);
	if (curDepth == nullptr)
		curDepth = renderFrame.GetRenderTarget("MainDepth");

	commandBuffer.BeginDebugMarker("Reset Draw Commands");
	_culler->ResetDrawCommands(renderFrame, commandBuffer);
	commandBuffer.EndDebugMarker();

	commandBuffer.BeginDebugMarker("Pass 1 Culling");
	_culler->DispatchPass1Culling(renderFrame, commandBuffer, camera, prevDepth);
	commandBuffer.EndDebugMarker();

	commandBuffer.BeginDebugMarker("Pass 1 Render Visible Objects");
	auto pass1BeginInfo = pass1RenderPass.CreateRenderPassBeginInfo(framebuffer);
	commandBuffer.BeginRenderPass(pass1BeginInfo);
	DrawIndirectInternal(renderFrame, commandBuffer, *_indirectCommandBuffer, perShader, perDraw);
	commandBuffer.EndRenderPass();
	commandBuffer.EndDebugMarker();

	commandBuffer.BeginDebugMarker("Pass 2 Culling");
	_culler->DispatchPass2Culling(renderFrame, commandBuffer, camera, curDepth);
	commandBuffer.EndDebugMarker();

	commandBuffer.BeginDebugMarker("Pass 2 Render Newly Visible Objects");
	auto pass2BeginInfo = pass2RenderPass.CreateRenderPassBeginInfo(framebuffer);
	commandBuffer.BeginRenderPass(pass2BeginInfo);
	DrawIndirectInternal(renderFrame, commandBuffer, *_culler->GetPass2IndirectCommandBuffer(), perShader, perDraw);

	if (postDraw)
	{
		commandBuffer.BeginDebugMarker("Post Draw");
		postDraw();
		commandBuffer.EndDebugMarker();
	}

	commandBuffer.EndRenderPass();
	commandBuffer.EndDebugMarker();
}

void Core::RendererBatches::DrawIndirect(
	RenderFrame& renderFrame,
	CommandBuffer& commandBuffer,
	function<void(shared_ptr<Shader>)> perShader,
	function<void(shared_ptr<Material>)> perDraw)
{
	DrawIndirectInternal(renderFrame, commandBuffer, *_indirectCommandBuffer, perShader, perDraw);
}

void Core::RendererBatches::DrawIndirect(RenderFrame& renderFrame, CommandBuffer& commandBuffer, DescriptorSetBuilder& builder, function<void(shared_ptr<Material>)> perDraw)
{
	if (_indirectDrawBuffer.GetDrawCount() == 0)
		return;

	auto* meshBufferManager = renderFrame.GetMeshBufferManager();

	for (auto& [shaderHash, shaderBatch] : _shaderBatches)
	{
		auto shader = shaderBatch.SharedShader.lock();
		if (!shader)
			continue;

		auto vertexAttibuteNames = shader->GetVertexAttirbuteNames();
		commandBuffer.BindVertexBuffers(meshBufferManager->GetVertexBuffers(vertexAttibuteNames), 0);
		commandBuffer.BindIndexBuffer(meshBufferManager->GetIndexBuffer(), meshBufferManager->GetIndexType());

		commandBuffer.BindPipeline(shaderBatch.Pipeline);

		// Batch fills its bindings, then builds
		builder.SetStorageBuffer(1, _transformBatch.TransformBuffer);
		builder.SetStorageBuffer(2, _instanceBuffer);

		if (_needsMaterialIndexBuffer)
		{
			builder.SetUniformBuffer(8,
				const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
			builder.SetStorageBuffer(9, _materialIndexBuffer);
		}

		auto& resources = builder.Build();

		commandBuffer.BindDescriptorSet(
			renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS,
			*shader, builder.GetSetIndex(), resources);

		auto material = shaderBatch.MaterialBatches.begin()->second.Material.lock();
		perDraw(material);

		commandBuffer.DrawIndexedIndirect(
			*_indirectCommandBuffer,
			_indirectDrawBuffer.GetDrawCount(),
			static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
		);
	}
}

void Core::RendererBatches::AddBatch(Device& device, RenderPass& renderPass, PipelineState& pipelineState, uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh)
{
	auto materialPtr = material.lock();
	auto shaderPtr = materialPtr->GetShaderPtr().lock();

	auto key = shaderPtr->GetType();
	auto batch = _shaderBatches.find(key);
	if (batch == _shaderBatches.end())
	{
		// Add new shader batch
		ShaderBatch shaderBatch;

		shaderBatch.Pipeline = new Core::Pipeline(device, renderPass, *shaderPtr.get(), pipelineState);
		shaderBatch.SharedShader = shaderPtr;
		_shaderBatches[key] = shaderBatch;
	}

	auto materialName = materialPtr->GetName();

	auto& materialBatches = _shaderBatches[key].MaterialBatches;
	auto matPtr = materialBatches.find(materialName);
	if (matPtr == materialBatches.end())
	{
		// Add new material batch
		MaterialBatch materialBatch;
		materialBatch.Material = material;

		materialBatches.insert(make_pair(materialName, materialBatch));
	}

	auto& subMeshBatches = materialBatches[materialName].SubMeshBatches;

	auto subMeshPtr = subMesh.lock();
	string subMeshName = subMeshPtr->GetName();

	auto& subMeshBatch = subMeshBatches[subMeshName];
	
	// Set the firstInstance if its the first time adding this submesh batch
	if (subMeshBatch.Transforms.empty())
	{
		subMeshBatch.SubMesh = subMesh;
		subMeshBatch.FirstInstance = _instanceCount;
	}

	subMeshBatch.Transforms.push_back(entityId);
	_instanceCount++;
}

void Core::RendererBatches::CreateInstanceBuffer(Device& device)
{
	vector<uint> instanceData(_instanceCount);
	
	uint i = 0;
	for (auto& shaderBatch : _shaderBatches)
	{
		for (auto& materialBatch : shaderBatch.second.MaterialBatches)
		{
			for (auto& subMeshBatch : materialBatch.second.SubMeshBatches)
			{
				for (auto& transform : subMeshBatch.second.Transforms)
				{
					instanceData[i++] = transform;
				}
			}
		}
	}

	_instanceBuffer = new Core::Buffer(device, _instanceCount * sizeof(uint),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::DEVICE_LOCAL);

	Core::VkBufferJob<uint> job(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &_instanceBuffer, instanceData, true);
	Core::CommandBuffer::ImmediateSubmit(device, job);
}

void Core::RendererBatches::DrawIndirectInternal(RenderFrame& renderFrame, CommandBuffer& commandBuffer, Core::Buffer& indirectCommandBuffer, function<void(shared_ptr<Shader>)> perShader, function<void(shared_ptr<Material>)> perDraw)
{
	if (_indirectDrawBuffer.GetDrawCount() == 0)
		return;

	auto* meshBufferManager = renderFrame.GetMeshBufferManager();

	for (auto& [shaderHash, shaderBatch] : _shaderBatches)
	{
		auto shader = shaderBatch.SharedShader.lock();
		if (!shader)
			continue;

		auto vertexAttibuteNames = shader->GetVertexAttirbuteNames();
		commandBuffer.BindVertexBuffers(meshBufferManager->GetVertexBuffers(vertexAttibuteNames), 0);
		commandBuffer.BindIndexBuffer(meshBufferManager->GetIndexBuffer(), meshBufferManager->GetIndexType());

		commandBuffer.BindPipeline(shaderBatch.Pipeline);

		renderFrame.SetShaderStorageBuffer(*shader, 1, _transformBatch.TransformBuffer);
		renderFrame.SetShaderStorageBuffer(*shader, 2, _instanceBuffer);

		if (_needsMaterialIndexBuffer)
		{
			renderFrame.SetShaderUniformBuffer(*shader, 8, const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
			renderFrame.SetShaderStorageBuffer(*shader, 9, _materialIndexBuffer);
		}

		if (shader->UsesBindlessTextures())
		{
			commandBuffer.BindBindlessDescriptorSet(
				renderFrame,
				shaderBatch.Pipeline->GetPipelineBindPoint(),
				shader->GetPipelineLayout());
		}

		if (perShader)
			perShader(shader);

		commandBuffer.BindDescriptorSets(renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS, *shader);

		auto material = shaderBatch.MaterialBatches.begin()->second.Material.lock();

		perDraw(material);

		commandBuffer.DrawIndexedIndirect(
			indirectCommandBuffer,
			_indirectDrawBuffer.GetDrawCount(),
			static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
		);
	}
}

void Core::RendererBatches::DispatchFrustumOnlyCulling(RenderFrame& renderFrame,
	CommandBuffer& commandBuffer, const CameraBuffer& camera)
{
	_culler->DispatchFrustumOnlyCulling(renderFrame, commandBuffer, camera);
}
