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
#include "Graphics/ResourceCache.h"
#include "TransferJob.h"

using namespace Core;

Core::RendererBatches::RendererBatches(Device& device, TransformBatch& transformBatch)
	: _device(device)
	, _transformBatch(transformBatch)
	, _instanceBuffer(VK_NULL_HANDLE)
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

	if (_objectDataBuffer != VK_NULL_HANDLE)
		delete(_objectDataBuffer);

	if (_visibleCountsBuffer != VK_NULL_HANDLE)
		delete(_visibleCountsBuffer);

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

void Core::RendererBatches::PrepareGPUDrivenRendering(Device& device, bool needMaterialData)
{
	_needsMaterialIndexBuffer = needMaterialData;

	_indirectDrawBuffer.Clear();
	uint32_t globalFirstInstance = 0;

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
					instanceCount,
					globalFirstInstance
				);
				globalFirstInstance += instanceCount;

				//Prepare object data for each instance
				for (size_t i = 0; i < subMeshBatch.Transforms.size(); ++i)
				{
					GPUObjectData data{};
					data.boundingSphere = glm::vec4(
						allocation.boundingSphereCenter,
						allocation.boundingSphereRadius);
					data.transformIndex = subMeshBatch.Transforms[i];

					objectData.push_back(data);
				}
			}
		}
	}

	vector<Job*> jobs;

	// Indirect Command Buffer
	Core::VkBufferJob<DrawIndexedIndirectCommand> job(device,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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

	_cullingShader = device.GetResourceCache().RequestShader("Shaders/gpuCulling.comp");
	_cullingPipeline = make_unique<Pipeline>(device, *_cullingShader);

	_visibleCountsBuffer = new Core::Buffer(device,
		sizeof(uint32_t),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL);
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

void Core::RendererBatches::ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes)
{
	// Left
	planes[0] = glm::vec4(
		viewProj[0][3] + viewProj[0][0],
		viewProj[1][3] + viewProj[1][0],
		viewProj[2][3] + viewProj[2][0],
		viewProj[3][3] + viewProj[3][0]);

	// Right
	planes[1] = glm::vec4(
		viewProj[0][3] - viewProj[0][0],
		viewProj[1][3] - viewProj[1][0],
		viewProj[2][3] - viewProj[2][0],
		viewProj[3][3] - viewProj[3][0]);

	// Bottom
	planes[2] = glm::vec4(
		viewProj[0][3] + viewProj[0][1],
		viewProj[1][3] + viewProj[1][1],
		viewProj[2][3] + viewProj[2][1],
		viewProj[3][3] + viewProj[3][1]);

	// Top
	planes[3] = glm::vec4(
		viewProj[0][3] - viewProj[0][1],
		viewProj[1][3] - viewProj[1][1],
		viewProj[2][3] - viewProj[2][1],
		viewProj[3][3] - viewProj[3][1]);

	// Near
	planes[4] = glm::vec4(
		viewProj[0][3] + viewProj[0][2],
		viewProj[1][3] + viewProj[1][2],
		viewProj[2][3] + viewProj[2][2],
		viewProj[3][3] + viewProj[3][2]);

	// Far
	planes[5] = glm::vec4(
		viewProj[0][3] - viewProj[0][2],
		viewProj[1][3] - viewProj[1][2],
		viewProj[2][3] - viewProj[2][2],
		viewProj[3][3] - viewProj[3][2]);

	// Normalize planes
	for (int i = 0; i < 6; ++i)
	{
		float length = glm::length(glm::vec3(planes[i]));
		planes[i] /= length;
	}
}

void Core::RendererBatches::DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer, const CameraBuffer& camera)
{
	GPUCullData cullData{};
	cullData.drawCount = _instanceCount;

	glm::mat4 viewProj = camera.Projection * camera.View;
	ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

	//Reset visible counts to 0
	commandBuffer.FillBuffer(*_visibleCountsBuffer, 0, sizeof(uint32_t), 0);

	commandBuffer.BufferBarrier(
		*_visibleCountsBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	commandBuffer.BindPipeline(_cullingPipeline.get());

	renderFrame.SetShaderUniformBuffer(*_cullingShader, 0, &cullData);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 1, _objectDataBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 2, _transformBatch.TransformBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 3, _instanceBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 4, _indirectCommandBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 5, _visibleCountsBuffer);

	commandBuffer.BindDescriptorSets(
		renderFrame,
		_cullingPipeline->GetPipelineBindPoint(),
		*_cullingShader);

	uint32_t groupCount = (_instanceCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
	commandBuffer.Dispatch(groupCount, 1, 1);

	//Barrier for instance buffer and indirect draw buffer
	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
}