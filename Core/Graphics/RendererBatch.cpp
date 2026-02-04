#include "stdafx.h"
#include "Graphics/RendererBatch.h"
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
#include "TransferJob.h"
using namespace Core;

Core::RendererBatches::RendererBatches(TransformBatch& transformBatch)
	:_transformBatch(transformBatch), _instanceBuffer(VK_NULL_HANDLE)
{
}

Core::RendererBatches::~RendererBatches()
{
	if (_materialIndexBuffer != VK_NULL_HANDLE)
		delete(_materialIndexBuffer);

	if (_indirectCommandBuffer != VK_NULL_HANDLE)
		delete(_indirectCommandBuffer);

	if (_instanceBuffer != VK_NULL_HANDLE)
		delete(_instanceBuffer);

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
			auto& shader = materials[i]->GetShader();

			if (shader.GetPass() == "Geometry")
			{
				auto& material = materials[i];
				auto& subMesh = mesh->GetSubMeshes()[i];

				AddBatch(device, renderPass, pipelineState, entityId, material, subMesh);
			}
		}
	}

	CreateInstanceBuffer(device);
}

void Core::RendererBatches::PrepareIndirectCommands(Device& device, bool needMaterialData)
{
	_needsMaterialIndexBuffer = needMaterialData;

	_indirectDrawBuffer.Clear();

	uint32_t globalFirstInstance = 0;

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
					instanceCount,
					globalFirstInstance
				);

				globalFirstInstance += instanceCount;
			}
		}
	}

	vector<Job*> jobs;

	// Indirect Command Buffer
	_indirectCommandBuffer = new Core::Buffer(
		device, 
		MAX_DRAW_COMMANDS * IndirectDrawBuffer::GetDrawCommandSize(),
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
		MemoryType::DEVICE_LOCAL);

	Core::VkBufferJob<DrawIndexedIndirectCommand> job(device,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		&_indirectCommandBuffer,
		_indirectDrawBuffer.GetDrawCommands());
	jobs.push_back(&job);

	// Material Index SSBO
	_materialIndexBuffer = new Core::Buffer(
		device,
		MAX_DRAW_COMMANDS * sizeof(uint32_t),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL
	);

	Core::VkBufferJob<uint32_t> job2(device,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		&_materialIndexBuffer,
		_indirectDrawBuffer.GetMaterialIndices(), true);
	jobs.push_back(&job2);

	Core::CommandBuffer::ImmediateSubmit(device, jobs);
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


void Core::RendererBatches::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    function<void(shared_ptr<Shader>)> perShader, 
    function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw)
{
    for (auto&& shaderBatch : _shaderBatches)
    {
        commandBuffer.BindPipeline(shaderBatch.second.Pipeline);

		auto shader = shaderBatch.second.SharedShader.lock();
		perShader(shader);

		renderFrame.SetShaderStorageBuffer(*shader, 1, _transformBatch.TransformBuffer);
		renderFrame.SetShaderStorageBuffer(*shader, 2, _instanceBuffer);

		commandBuffer.BindDescriptorSets(
			renderFrame,
			VK_PIPELINE_BIND_POINT_GRAPHICS, *shader);

        //1. Material batch
        for (auto&& materialBatch : shaderBatch.second.MaterialBatches)
		{
			auto sharedMaterial = materialBatch.second.Material.lock();

			commandBuffer.BindDescriptorSets(
				renderFrame,
				VK_PIPELINE_BIND_POINT_GRAPHICS, *sharedMaterial);

			//2. SubMesh batch
            for (auto&& subMeshBatch : materialBatch.second.SubMeshBatches)
            {
                auto subMesh = subMeshBatch.second.SubMesh.lock();

                perDraw(sharedMaterial, subMesh);

                auto vertexAttibuteNames = sharedMaterial->GetShader().GetVertexAttirbuteNames();

                commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);
                commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());
                commandBuffer.DrawIndexed(
                    subMesh->GetIndexCount(), 
                    subMeshBatch.second.Transforms.size(),
                    subMeshBatch.second.FirstInstance);
            }
        }
    }
}

void Core::RendererBatches::DrawIndirect(
	RenderFrame& renderFrame,
	CommandBuffer& commandBuffer,
	function<void(shared_ptr<Shader>)> perShader,
	function<void(shared_ptr<Material>)> perDraw)
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
			renderFrame.SetShaderUniformBuffer(*shader, 7, const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
			renderFrame.SetShaderStorageBuffer(*shader, 8, _materialIndexBuffer);
		}
		
		if (perShader)
			perShader(shader);

		commandBuffer.BindDescriptorSets(renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS, *shader);

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
		//Add new shader batch
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
		//Add new material batch
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
