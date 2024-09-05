#include "stdafx.h"
#include "RendererBatch.h"
#include "Shader.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRenderer.h"
#include "PipelineState.h"
#include "Pipeline.h"
#include "RenderPass.h"
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
	auto matType = meshRenderer.GetMaterial().GetType();

	auto matPtr = Materials.find(matType);
	if (matPtr == Materials.end())
	{
		Materials.insert(make_pair(matType, &meshRenderer.GetMaterial()));
	}

	auto& batch = MeshBatches[matType][meshRenderer.GetMesh().GetModelPath()];
	batch.push_back(&meshRenderer);
}

void Core::RendererBatch::Sort(type_index type)
{
}
