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

Core::RendererBatch::RendererBatch(Device& device, Shader& shader, RenderPass& renderPass, PipelineState& pipelineState)
	:SharedShader(shader)
{
	Pipeline = new Core::Pipeline(device, renderPass, shader, pipelineState);
}

Core::RendererBatch::~RendererBatch()
{
	delete(Pipeline);
}

void Core::RendererBatch::Add(Mesh& mesh)
{
	auto& transform = mesh.GetEntity().GetTransform();
	auto materials = mesh.GetMaterials();
	auto subMeshes = mesh.GetSubMeshes();

	for (size_t i = 0; i < materials.size(); ++i)
	{
		auto material = materials[i];

		auto name = material->GetName();

		auto matPtr = Materials.find(name);
		if (matPtr == Materials.end())
		{
			Materials.insert(make_pair(name, material));
		}

		auto subMesh = subMeshes[i];
		SubMeshBatches[name][subMesh->GetName()] = subMesh;
		Transforms[subMesh->GetName()].push_back(&transform);
	}
}

void Core::RendererBatch::Add(Mesh& mesh, weak_ptr<Material> material)
{
	auto& transform = mesh.GetEntity().GetTransform();
	auto subMeshes = mesh.GetSubMeshes();

	auto name = material.lock()->GetName();

	auto matPtr = Materials.find(name);
	if (matPtr == Materials.end())
	{
		Materials.insert(make_pair(name, material));
	}

	for (size_t i = 0; i < subMeshes.size(); ++i)
	{
		auto subMesh = subMeshes[i];
		SubMeshBatches[name][subMesh->GetName()] = subMesh;
		Transforms[subMesh->GetName()].push_back(&transform);
	}
}

void Core::RendererBatch::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame,
	function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> loop)
{
	commandBuffer.BindPipeline(Pipeline);

	//1. Material batch
	for (auto&& material : Materials)
	{
		auto sharedMaterial = material.second.lock();

		commandBuffer.BindDescriptorSets(
			VK_PIPELINE_BIND_POINT_GRAPHICS, *sharedMaterial, currentFrame);

		//2. SubMesh batch
		auto& subMeshBatches = SubMeshBatches[material.first];

		for (auto&& subMeshBatch : subMeshBatches)
		{
			RendererBatch::Sort();

			auto subMesh = subMeshBatch.second.lock();
			auto& transforms = Transforms[subMesh->GetName()];

			loop(sharedMaterial, subMesh);
		}
	}
}

void Core::RendererBatch::Sort()
{
}

Core::RendererBatches::RendererBatches()
{
}

Core::RendererBatches::~RendererBatches()
{
	delete(_instanceBuffer);

	for (auto&& batch : _batches)
	{
		delete(batch.second);
	}

	_batches.clear();
}

void Core::RendererBatches::Prepare(Device& device, RenderPass& renderPass, PipelineState& pipelineState, Scene& scene)
{
	auto meshes = scene.GetComponents<Core::Mesh>();

	for (auto&& mesh : meshes)
	{
		auto materials = mesh->GetMaterials();
		for (size_t i = 0; i < materials.size(); ++i)
		{
			auto& shader = materials[i]->GetShader();

			if (shader.GetPass() == "Geometry")
			{
				auto key = shader.GetType();
				auto batch = _batches[key];
				if (batch == nullptr)
				{
					batch = new RendererBatch(device, shader, renderPass, pipelineState);
					_batches[key] = batch;
				}

				batch->Add(*mesh);
			}
		}
	}

	uint meshCount = meshes.size();

	vector<uint> instanceData(meshCount);

	for (u32 i = 0; i < meshCount; ++i)
	{
		auto& transform = meshes[i]->GetEntity().GetTransform();
		transforms[i] = transform.GetMatrix();
	}

	_instanceBuffer = new Core::Buffer(device, meshCount * sizeof(uint),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::DEVICE_LOCAL);

	Core::CommandBuffer::ImmediateSubmit(device, [&](Core::CommandBuffer& commandBuffer)
	{
		Core::VkBufferJob<uint> job(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &_instanceBuffer, instanceData, true);
		job.commandBuffer = &commandBuffer;
		job.Execute();
	});
}

void Core::RendererBatches::PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, Scene& scene)
{
	auto meshes = scene.GetComponents<Core::Mesh>();

	for (auto&& mesh : meshes)
	{
		_batches[0]->Add(*mesh, material);
	}

	uint meshCount = meshes.size();

	vector<uint> instanceData(meshCount);

	for (u32 i = 0; i < meshCount; ++i)
	{
		auto& transform = meshes[i]->GetEntity().GetTransform();
		transforms[i] = transform.GetMatrix();
	}

	_instanceBuffer = new Core::Buffer(device, meshCount * sizeof(uint),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::DEVICE_LOCAL);

	Core::CommandBuffer::ImmediateSubmit(device, [&](Core::CommandBuffer& commandBuffer)
		{
			Core::VkBufferJob<uint> job(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &_instanceBuffer, instanceData, true);
			job.commandBuffer = &commandBuffer;
			job.Execute();
		});
}

void Core::RendererBatches::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, function<void(shared_ptr<Material>)> setMaterial, function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> loop)
{
	for (auto&& batch : _batches)
	{
		for (auto&& material : batch.second->Materials)
		{
			auto sharedMat = material.second.lock();
			setMaterial(sharedMat);
		}

		batch.second->Draw(commandBuffer, currentFrame, loop);
	}
}
