#include "stdafx.h"
#include "GeometryRenderPass.h"
#include "Mesh.h"
#include "Scene.h"
#include "Material.h"
#include "Pipeline.h"
#include "PerspectiveCamera.h"
#include "CommandBuffer.h"
#include "Framebuffer.h"
#include "SwapChain.h"
#include "CommandPool.h"

namespace Core
{
	GeometryRenderPass::GeometryRenderPass(Device& device, 
		VkExtent2D extent, Scene& scene, SwapChain& swapChain)
		:RenderPass(device), _scene(scene), _material(nullptr), _pipeline(nullptr)
	{
		CreateColorRenderTarget(extent, swapChain.GetImageFormat());
		CreateDepthRenderTarget(extent);
		CreateRenderPass();

		_framebuffer = new Core::Framebuffer(device, swapChain, *this);
	}

	GeometryRenderPass::~GeometryRenderPass()
	{
		delete(_pipeline);
		delete(_material);
		delete(_framebuffer);
	}

	void GeometryRenderPass::Prepare(CommandPool& commandPool)
	{
		//todo : collect meshes.
		//todo : push back materials.

		_material = new Core::Material(_device, commandPool);

		_pipeline = new Core::Pipeline(_device,
			*this, _material->GetShader());
	}

	void GeometryRenderPass::Draw(
		CommandBuffer& commandBuffer, 
		uint32_t currentFrame, uint32_t imageIndex)
	{
		PerspectiveCamera& camera = _scene.GetMainCamera();

		//todo : sort

		auto framebuffer = _framebuffer->GetHandle(imageIndex);
		auto renderPassBeginInfo = CreateRenderPassBeginInfo(framebuffer, _framebuffer->GetExtent());
		commandBuffer.BeginRenderPass(renderPassBeginInfo);
		commandBuffer.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
			_pipeline->GetGraphicsPipeline());

		_material->SetBuffer(currentFrame, 0, &camera.Matrices);

		commandBuffer.BindDescriptorSets(
			VK_PIPELINE_BIND_POINT_GRAPHICS, _material->GetShader(), currentFrame);

		auto meshes = _scene.GetComponents<Core::Mesh>();
		for (auto mesh : meshes)
		{
			mesh->DrawFrame(commandBuffer, *_material);
			commandBuffer.Flush(*_material);
			commandBuffer.BindVertexBuffers(mesh->GetVertexBuffer());
			commandBuffer.BindIndexBuffer(mesh->GetIndexBuffer(), VK_INDEX_TYPE_UINT32);
			commandBuffer.DrawIndexed(mesh->GetIndexCount(), 1);
		}

		commandBuffer.EndRenderPass();
	}
}
