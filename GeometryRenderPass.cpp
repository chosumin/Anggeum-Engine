#include "stdafx.h"
#include "GeometryRenderPass.h"
#include "MeshRenderer.h"
#include "Scene.h"
#include "PerspectiveCamera.h"
#include "CommandBuffer.h"
#include "Framebuffer.h"
#include "SwapChain.h"
#include "CommandPool.h"
#include "Pipeline.h"
#include "Material.h"
#include "Mesh.h"
#include "Shader.h"
#include "RendererBatch.h"

namespace Core
{
	GeometryRenderPass::GeometryRenderPass(Device& device, 
		Scene& scene, SwapChain& swapChain)
		:RenderPass(device), _scene(scene)
	{
		auto extent = swapChain.GetSwapChainExtent();
		CreateColorRenderTarget(extent, swapChain.GetImageFormat());
		CreateDepthRenderTarget(extent);
		CreateRenderPass();

		_framebuffer = new Core::Framebuffer(device, swapChain, *this);
	}

	GeometryRenderPass::~GeometryRenderPass()
	{
		for (auto&& batch : _batches)
		{
			delete(batch.second);
		}

		_batches.clear();
	}

	void GeometryRenderPass::Prepare()
	{
		auto meshRenderers = _scene.GetComponents<Core::MeshRenderer>();
		for (auto&& meshRenderer : meshRenderers)
		{
			auto& shader = meshRenderer->GetMaterial().GetShader();

			if (shader.GetPass() == "Geometry")
			{
				auto key = shader.GetType();
				auto batch = _batches[key];
				if (batch == nullptr)
				{
					batch = new RendererBatch(_device, shader, *this);
					_batches[key] = batch;
				}

				batch->Add(*meshRenderer);
			}
		}
	}

	void GeometryRenderPass::Draw(
		CommandBuffer& commandBuffer, 
		uint32_t currentFrame, uint32_t imageIndex)
	{
		PerspectiveCamera& camera = _scene.GetMainCamera();

		auto framebuffer = _framebuffer->GetHandle(imageIndex);
		auto renderPassBeginInfo = CreateRenderPassBeginInfo(framebuffer, _framebuffer->GetExtent());
		commandBuffer.BeginRenderPass(renderPassBeginInfo);

		for (auto&& batch : _batches)
		{
			auto& pipeline = batch.second->Pipeline;
			commandBuffer.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline->GetGraphicsPipeline());

			auto& materials = batch.second->Materials;
			for (auto&& material : materials)
			{
				material.second->SetBuffer(currentFrame, 0, &camera.Matrices);

				commandBuffer.BindDescriptorSets(
					VK_PIPELINE_BIND_POINT_GRAPHICS, material.second->GetShader(), currentFrame);

				auto& meshRenderers = 
					batch.second->MeshRenderers[material.first];

				RendererBatch::Sort(material.first);

				for (auto&& meshRenderer : meshRenderers)
				{
					meshRenderer->Draw();

					commandBuffer.Flush(*material.second);

					auto& mesh = meshRenderer->GetMesh();
					commandBuffer.BindVertexBuffers(mesh.GetVertexBuffer());
					commandBuffer.BindIndexBuffer(mesh.GetIndexBuffer(), VK_INDEX_TYPE_UINT32);
					commandBuffer.DrawIndexed(mesh.GetIndexCount(), 1);
				}
			}
		}
		
		commandBuffer.EndRenderPass();
	}
}
