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
#include "TransferJob.h"
using namespace Core;

Core::RendererBatches::RendererBatches(TransformBatch& transformBatch)
	:_transformBatch(transformBatch), _instanceBuffer(VK_NULL_HANDLE)
{
}

Core::RendererBatches::~RendererBatches()
{
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


void Core::RendererBatches::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, function<void(shared_ptr<Material>)> perMaterial, function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw)
{
	for (auto&& shaderBatch : _shaderBatches)
	{
		for (auto&& material : shaderBatch.second.MaterialBatches)
		{
			auto sharedMat = material.second.Material.lock();
			perMaterial(sharedMat);
		}

		commandBuffer.BindPipeline(shaderBatch.second.Pipeline);

		//1. Material batch
		for (auto&& materialBatch : shaderBatch.second.MaterialBatches)
		{
			auto sharedMaterial = materialBatch.second.Material.lock();

			sharedMaterial->SetStorageBuffer(1, _transformBatch.TransformBuffer);
			sharedMaterial->SetStorageBuffer(2, _instanceBuffer);

			commandBuffer.BindDescriptorSets(
				VK_PIPELINE_BIND_POINT_GRAPHICS, *sharedMaterial, currentFrame);

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
