#include "stdafx.h"
#include "RendererBatch.h"
#include "VulkanWrapper/Shader.h"
#include "Material.h"
#include "Entity.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "Core/SubMesh.h"
#include "VulkanWrapper/PipelineState.h"
#include "VulkanWrapper/Pipeline.h"
#include "VulkanWrapper/RenderPass.h"
#include "VulkanWrapper/CommandBuffer.h"
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

		auto hash = material->GetHash();

		auto matPtr = Materials.find(hash);
		if (matPtr == Materials.end())
		{
			Materials.insert(make_pair(hash, material));
		}

		auto subMesh = subMeshes[i];
		SubMeshBatches[hash][subMesh->GetName()] = subMesh;
		Transforms[subMesh->GetName()].push_back(&transform);
	}
}

void Core::RendererBatch::Add(Mesh& mesh, Material& material)
{
	auto& transform = mesh.GetEntity().GetTransform();
	auto subMeshes = mesh.GetSubMeshes();

	auto hash = material.GetHash();

	auto matPtr = Materials.find(hash);
	if (matPtr == Materials.end())
	{
		Materials.insert(make_pair(hash, &material));
	}

	for (size_t i = 0; i < subMeshes.size(); ++i)
	{
		auto subMesh = subMeshes[i];
		SubMeshBatches[hash][subMesh->GetName()] = subMesh;
		Transforms[subMesh->GetName()].push_back(&transform);
	}
}

void Core::RendererBatch::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame)
{
	commandBuffer.BindPipeline(Pipeline);

	//1. Material batch
	for (auto&& material : Materials)
	{
		auto& subMeshBatches = SubMeshBatches[material.first];

		commandBuffer.BindDescriptorSets(
			VK_PIPELINE_BIND_POINT_GRAPHICS, *material.second, currentFrame);

		auto vertexAttibuteNames = material.second->GetShader().GetVertexAttirbuteNames();

		//2. SubMesh batch
		for (auto&& subMeshBatch : subMeshBatches)
		{
			RendererBatch::Sort();

			auto& subMesh = subMeshBatch.second;
			auto& transforms = Transforms[subMesh->GetName()];

			//3. Transform loop
			for (size_t i = 0; i < transforms.size(); ++i)
			{
				material.second->SetPushConstants<mat4>(transforms[i]->GetMatrix());
			}

			commandBuffer.PushConstants(*material.second);

			commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);

			commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());

			commandBuffer.DrawIndexed(subMesh->GetIndexCount(), static_cast<uint32_t>(transforms.size()));
		}
	}
}

void Core::RendererBatch::Sort()
{
}
