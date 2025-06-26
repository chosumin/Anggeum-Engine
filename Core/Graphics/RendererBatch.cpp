#include "stdafx.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/CommandBuffer.h"
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
	function<void(shared_ptr<Material>, shared_ptr<SubMesh>, vector<Transform*>&)> loop)
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

			loop(sharedMaterial, subMesh, transforms);
		}
	}
}

void Core::RendererBatch::Sort()
{
}
