#include "stdafx.h"
#include "ShadowRenderPass.h"
#include "Scene.h"
#include "SwapChain.h"
#include "InstanceData.h"
#include "PerspectiveCamera.h"
#include "Entity.h"
#include "MeshRenderer.h"
#include "RendererBatch.h"
#include "ShadowShader.h"
#include "Framebuffer.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "MaterialFactory.h"
#include "Material.h"
#include "Mesh.h"
using namespace Core;

Core::ShadowRenderPass::ShadowRenderPass(Device& device, Scene& scene, SwapChain& swapChain, RenderTarget* depthRenderTarget)
	:RenderPass(device), _scene(scene)
{
	auto extent = swapChain.GetSwapChainExtent();

	auto cameraEntity = make_unique<Entity>(-1, "shadow camera");
	auto camera = make_unique<PerspectiveCamera>(*cameraEntity, (float)extent.width, (float)extent.height);
	_shadowCamera = camera.get();

	_scene.AddComponent(move(camera), *cameraEntity);
	_scene.AddEntity(move(cameraEntity));

	_material = MaterialFactory::CreateMaterial(device, MaterialType::SHADOW);

	CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	CreateRenderPass();

	_framebuffer = new Framebuffer(device, swapChain, *this);
	_instanceBuffer = new InstanceBuffer(device);
}

Core::ShadowRenderPass::~ShadowRenderPass()
{
	for (auto&& batch : _batches)
	{
		delete(batch.second);
	}

	_batches.clear();

	delete(_instanceBuffer);
}

void Core::ShadowRenderPass::Prepare()
{
	auto meshRenderers = _scene.GetComponents<Core::MeshRenderer>();
	for (auto&& meshRenderer : meshRenderers)
	{
		auto key = _material->GetShader().GetType();
		auto batch = _batches[key];
		if (batch == nullptr)
		{
			batch = new RendererBatch(_device, _material->GetShader(), *this);
			_batches[key] = batch;
		}

		batch->Add(*meshRenderer);
	}
}

void Core::ShadowRenderPass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	auto framebuffer = _framebuffer->GetHandle(imageIndex);
	auto renderPassBeginInfo = CreateRenderPassBeginInfo(framebuffer, _framebuffer->GetExtent());
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	_material->SetBuffer(currentFrame, 0, &_shadowCamera->Matrices);

	for (auto&& batch : _batches)
	{
		auto& pipeline = batch.second->Pipeline;
		commandBuffer.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline->GetGraphicsPipeline());

		auto& materials = batch.second->Materials;
		for (auto&& material : materials)
		{
			auto& meshBatches =
				batch.second->MeshBatches[material.first];

			commandBuffer.BindDescriptorSets(
				VK_PIPELINE_BIND_POINT_GRAPHICS, _material->GetShader(), currentFrame);

			for (auto&& meshBatch : meshBatches)
			{
				RendererBatch::Sort(material.first);

				commandBuffer.Flush(*material.second);

				auto& meshRenderers = meshBatch.second;
				for (size_t i = 0; i < meshRenderers.size(); ++i)
				{
					auto& entity = meshRenderers[i]->GetEntity();
					_instanceBuffer->SetBuffer(i, entity.GetTransform());
				}
				_instanceBuffer->Copy();

				auto& mesh = meshBatch.second[0]->GetMesh();
				commandBuffer.BindVertexBuffers(mesh.GetVertexBuffer(), 0);
				commandBuffer.BindVertexBuffers(
					_instanceBuffer->GetBuffer(), 1);

				commandBuffer.BindIndexBuffer(mesh.GetIndexBuffer(), VK_INDEX_TYPE_UINT32);

				commandBuffer.DrawIndexed(mesh.GetIndexCount(), static_cast<uint32_t>(meshRenderers.size()));
			}
		}
	}

	commandBuffer.EndRenderPass();
}
