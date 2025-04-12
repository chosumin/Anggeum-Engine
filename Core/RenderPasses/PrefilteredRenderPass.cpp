#include "stdafx.h"
#include "PrefilteredRenderPass.h"
#include "Scene.h"
#include "Entity.h"
#include "Components/Mesh.h"
#include "VulkanWrapper/CommandBuffer.h"
#include "VulkanWrapper/Pipeline.h"
#include "VulkanWrapper/Texture.h"
#include "VulkanWrapper/Shader.h"
#include "Utils/Utility.h"
#include "SubMesh.h"
#include "Material.h"
using namespace Core;

#define PI 3.1415926535897932384626433832795

Core::PrefilteredRenderPass::PrefilteredRenderPass(Device& device, Scene& scene,
	Texture* offscreen, Texture* prefilteredCubemap)
	:RenderPass(device), _scene(scene), _prefilteredCubemap(prefilteredCubemap), _colorRenderTarget(offscreen)
{
	CreateColorAttachment(offscreen, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);

	CreateRenderPass();
	CreateFrameBuffer(offscreen->GetImage());

	uint32_t hash = Utility::HashCode("Prefiltered");
	_material = new Material(device, "Prefiltered", hash);
}

Core::PrefilteredRenderPass::~PrefilteredRenderPass()
{
	delete(_skyboxPipeline);
	delete(_material);
}

void Core::PrefilteredRenderPass::Prepare()
{
	auto meshes = _scene.GetComponents<Core::Mesh>();

	auto it = find_if(meshes.begin(), meshes.end(), [](Mesh* mesh)
	{
		auto material = mesh->GetMaterials()[0];
		auto& shader = material->GetShader();
		return shader.GetPass() == "Skybox";
	});

	if (it != meshes.end())
	{
		auto skybox = *it;
		
		_sky = skybox->GetSubMeshes()[0];
		auto material = skybox->GetMaterials()[0];
		_skyCubemap = material->GetTexture(1);
		auto& shader = _material->GetShader();

		auto pipelineState = *_pipelineState;

		auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
		depthInfo.depthWriteEnable = VK_FALSE;
		depthInfo.depthTestEnable = VK_FALSE;

		_skyboxPipeline = new Pipeline(_device, *this, shader, pipelineState);
	}

	_mvpMatrices = {
		glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
	};
}

void Core::PrefilteredRenderPass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	{
		commandBuffer.TransitionImageLayout(*_prefilteredCubemap->GetImage(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}

	uint32_t mipLevels = _prefilteredCubemap->GetMipLevels();
	uint32_t layers = _prefilteredCubemap->GetLayers();

	auto extent = GetBufferExtent2D();

	for (uint32_t m = 0; m < mipLevels; ++m)
	{
		for (uint32_t layer = 0; layer < layers; ++layer)
		{
			VkExtent2D mipExtent;
			mipExtent.width = static_cast<uint32_t>(extent.width * pow(0.5f, m));
			mipExtent.height = static_cast<uint32_t>(extent.height * pow(0.5f, m));

			commandBuffer.SetViewportAndScissor(mipExtent);

			commandBuffer.BeginRenderPass(CreateRenderPassBeginInfo(imageIndex));

			_material->SetBuffer(0, _skyCubemap);

			_material->SetPushConstants<mat4>(glm::perspective((float)(PI / 2.0), 1.0f, 0.1f, 512.0f) * _mvpMatrices[layer]);
			commandBuffer.PushConstants(*_material, 0);

			_prefilterEnv.Roughness = (float)m / (float)(mipLevels - 1);
			_material->SetPushConstants<PrefilterEnv>(_prefilterEnv);
			commandBuffer.PushConstants(*_material, 1);

			commandBuffer.BindPipeline(_skyboxPipeline);

			commandBuffer.BindDescriptorSets(
				_skyboxPipeline->GetPipelineBindPoint(), *_material, currentFrame);

			auto vertexAttibuteNames = _material->GetShader().GetVertexAttirbuteNames();

			commandBuffer.BindVertexBuffers(_sky->GetVertexBuffers(vertexAttibuteNames), 0);

			commandBuffer.BindIndexBuffer(_sky->GetIndexBuffer(), _sky->GetIndexType());

			commandBuffer.DrawIndexed(_sky->GetIndexCount(), 1);

			commandBuffer.EndRenderPass();

			commandBuffer.TransitionImageLayout(*_colorRenderTarget->GetImage(),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

			commandBuffer.CopyImage(*_colorRenderTarget->GetImage(), *_prefilteredCubemap->GetImage(), 0, 0, m, layer);

			commandBuffer.TransitionImageLayout(*_colorRenderTarget->GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}
	}

	{
		commandBuffer.TransitionImageLayout(*_prefilteredCubemap->GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}