#include "stdafx.h"
#include "GeometryRenderPass.h"
#include "Scene.h"
#include "Components/PerspectiveCamera.h"
#include "VulkanWrapper/CommandBuffer.h"
#include "VulkanWrapper/Framebuffer.h"
#include "VulkanWrapper/SwapChain.h"
#include "VulkanWrapper/CommandPool.h"
#include "VulkanWrapper/Pipeline.h"
#include "VulkanWrapper/Shader.h"
#include "Material.h"
#include "Core/Components/Mesh.h"
#include "RendererBatch.h"
#include "Component.h"

namespace Core
{
	GeometryRenderPass::GeometryRenderPass(Device& device, 
		Scene& scene, SwapChain& swapChain, RenderTarget* colorRenderTarget, RenderTarget* depthRenderTarget, RenderTarget* shadowRenderTarget)
		:RenderPass(device), _scene(scene), _shadowRenderTarget(shadowRenderTarget)
	{
		auto extent = swapChain.GetSwapChainExtent();
		CreateColorAttachment(colorRenderTarget,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
		CreateDepthAttachment(depthRenderTarget,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE);
		CreateRenderPass();

		_framebuffer = new Framebuffer(device, swapChain, *this);

		auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
		multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
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
		auto meshes = _scene.GetComponents<Core::Mesh>();
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
						batch = new RendererBatch(_device, shader, *this, *_pipelineState);
						_batches[key] = batch;
					}

					batch->Add(*mesh);
				}
			}
		}
	}

	void GeometryRenderPass::Draw(
		CommandBuffer& commandBuffer, 
		uint32_t currentFrame, uint32_t imageIndex)
	{
		_shadowRenderTarget->TransitionImageLayout(commandBuffer,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		auto framebuffer = _framebuffer->GetHandle(imageIndex);
		auto renderPassBeginInfo = CreateRenderPassBeginInfo(framebuffer, _framebuffer->GetExtent());
		commandBuffer.BeginRenderPass(renderPassBeginInfo);

		PerspectiveCamera* camera = _scene.GetMainCamera();

		for (auto&& batch : _batches)
		{
			for (auto&& material : batch.second->Materials)
			{
				material.second->SetBuffer(currentFrame, 0, &camera->Matrices);
				material.second->SetBuffer(4, _shadowRenderTarget);
				material.second->SetBuffer(currentFrame, 5, &_shadowBuffer->Projection);
				material.second->SetBuffer(currentFrame, 7, &_lightBuffer);
				material.second->SetBuffer(currentFrame);
			}

			batch.second->Draw(commandBuffer, currentFrame);
		}

		commandBuffer.EndRenderPass();
	}
}
