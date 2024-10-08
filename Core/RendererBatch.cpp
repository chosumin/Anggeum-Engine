#include "stdafx.h"
#include "RendererBatch.h"
#include "VulkanWrapper/Shader.h"
#include "Material.h"
#include "Mesh.h"
#include "Components/MeshRenderer.h"
#include "VulkanWrapper/PipelineState.h"
#include "VulkanWrapper/Pipeline.h"
#include "VulkanWrapper/RenderPass.h"
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

void Core::RendererBatch::Add(MeshRenderer& meshRenderer)
{
	auto hash = meshRenderer.GetMaterial().GetHash();

	auto matPtr = Materials.find(hash);
	if (matPtr == Materials.end())
	{
		Materials.insert(make_pair(hash, &meshRenderer.GetMaterial()));
	}

	auto& batch = MeshBatches[hash][meshRenderer.GetMesh().GetModelPath()];
	batch.push_back(&meshRenderer);
}

void Core::RendererBatch::Sort()
{
}
