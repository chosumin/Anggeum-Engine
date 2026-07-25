#include "stdafx.h"
#include "PreEnvironmentPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
#include "Utils/Utility.h"

using namespace Core;

#define PI 3.1415926535897932384626433832795

Core::PreEnvironmentPass::PreEnvironmentPass(Device& device, 
    WorkerThreadManager& workerThreadManager, Scene& scene,
    Texture* offscreen, Texture* irradianceCubemap, Texture* prefilteredCubemap)
    : RendererPass(device, workerThreadManager)
    , _scene(scene)
    , _colorRenderTarget(offscreen)
    , _irradianceCubemap(irradianceCubemap)
    , _prefilteredCubemap(prefilteredCubemap)
    , _irradianceShader(&device.GetResourceCache().LoadMaterial("irradiance", "Irradiance").Get().GetShaderHandle().Get())
    , _prefilteredShader(&device.GetResourceCache().LoadMaterial("prefiltered", "Prefiltered").Get().GetShaderHandle().Get())
{
    _renderPass->CreateColorAttachment(offscreen, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
    _renderPass->CreateRenderPass();

    _framebuffer = make_unique<Framebuffer>(_device, *_renderPass, vector<Texture*>{ offscreen });
}

Core::PreEnvironmentPass::~PreEnvironmentPass()
{
    delete(_irradiancePipeline);
    delete(_prefilteredPipeline);
}

void Core::PreEnvironmentPass::Initialize()
{
    auto meshes = _scene.GetComponents<Core::Mesh>();

    auto it = find_if(meshes.begin(), meshes.end(), [](Mesh* mesh)
    {
        auto& material = mesh->GetMaterials()[0].Get();
        auto& shader = material.GetShaderHandle().Get();
        return shader.GetPass() == "Skybox";
    });

    Handle<Texture> skyCubemap;

    if (it != meshes.end())
    {
        auto skybox = *it;
        
        _sky = &skybox->GetSubMeshes()[0].Get();
        auto& material = skybox->GetMaterials()[0].Get();
        skyCubemap = material.GetTexture(1);

        auto pipelineState = *_pipelineState;

        auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
        depthInfo.depthWriteEnable = VK_FALSE;
        depthInfo.depthTestEnable = VK_FALSE;

        _irradiancePipeline = new Pipeline(_device, *_renderPass, *_irradianceShader, pipelineState);
        _prefilteredPipeline = new Pipeline(_device, *_renderPass, *_prefilteredShader, pipelineState);

        // Resolve the sky's vertex/index buffers here (main thread) so Draw() on the
        // worker thread binds raw pointers instead of resolving pool handles.
        _irradianceVertexBuffers = _sky->GetVertexBuffers(_irradianceShader->GetVertexAttirbuteNames());
        _prefilteredVertexBuffers = _sky->GetVertexBuffers(_prefilteredShader->GetVertexAttirbuteNames());
        _skyIndexBuffer = &_sky->GetIndexBuffer();
        _skyIndexType = _sky->GetIndexType();
    }

    _mvpMatrices = {
        glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
    };

    _delta.Phi = (2.0f * float(PI)) / 180.0f;
    _delta.Theta = (0.5f * float(PI)) / 64.0f;

    _skyCubemap = skyCubemap;
}

void Core::PreEnvironmentPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
    DrawIrradiance(renderFrame, commandBuffer);
    DrawPrefiltered(renderFrame, commandBuffer);
}

void Core::PreEnvironmentPass::DrawIrradiance(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
    auto& shader = *_irradianceShader;

    commandBuffer.CreateBarrierBatch()
        .Image(*_irradianceCubemap,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        .Submit();

    uint32_t mipLevels = _irradianceCubemap->GetMipLevels();
    uint32_t layers = _irradianceCubemap->GetLayers();

    auto extent = _colorRenderTarget->GetExtent();
    VkExtent2D extent2D = { extent.width, extent.height };

    for (uint32_t m = 0; m < mipLevels; ++m)
    {
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            VkExtent2D mipExtent;
            mipExtent.width = static_cast<uint32_t>(extent2D.width * pow(0.5f, m));
            mipExtent.height = static_cast<uint32_t>(extent2D.height * pow(0.5f, m));

            commandBuffer.SetViewportAndScissor(mipExtent);

            auto beginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer);
            commandBuffer.BeginRenderPass(beginInfo);

            mat4 viewProjection = glm::perspective((float)(PI / 2.0), 1.0f, 0.1f, 512.0f) * _mvpMatrices[layer];
            commandBuffer.PushConstants(shader, 0, viewProjection);
            commandBuffer.PushConstants(shader, 1, _delta);

            commandBuffer.BindPipeline(_irradiancePipeline);

            auto irradianceBuilder = renderFrame.GetResources().CreateDescriptorSetBuilder(shader, 0);
            irradianceBuilder.SetTextureBuffer(0, _skyCubemap);
            auto& irradianceResources = irradianceBuilder.Build();
            commandBuffer.BindDescriptorSet(
                _irradiancePipeline->GetPipelineBindPoint(),
                shader, irradianceResources);

            commandBuffer.BindVertexBuffers(_irradianceVertexBuffers, 0);
            commandBuffer.BindIndexBuffer(*_skyIndexBuffer, _skyIndexType);
            commandBuffer.DrawIndexed(_sky->GetIndexCount(), 1);

            commandBuffer.EndRenderPass();

            commandBuffer.CreateBarrierBatch()
                .Image(*_colorRenderTarget,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
                .Submit();

            commandBuffer.CopyImage(*_colorRenderTarget, 
                *_irradianceCubemap, 0, 0, m, layer);

            commandBuffer.CreateBarrierBatch()
                .Image(*_colorRenderTarget,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .Submit();
        }
    }

    commandBuffer.CreateBarrierBatch()
        .Image(*_irradianceCubemap,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}

void Core::PreEnvironmentPass::DrawPrefiltered(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
    auto& shader = *_prefilteredShader;

    commandBuffer.CreateBarrierBatch()
        .Image(*_prefilteredCubemap,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        .Submit();

    uint32_t mipLevels = _prefilteredCubemap->GetMipLevels();
    uint32_t layers = _prefilteredCubemap->GetLayers();

    auto extent = _colorRenderTarget->GetExtent();
    VkExtent2D extent2D = { extent.width, extent.height };

    for (uint32_t m = 0; m < mipLevels; ++m)
    {
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            VkExtent2D mipExtent;
            mipExtent.width = static_cast<uint32_t>(extent2D.width * pow(0.5f, m));
            mipExtent.height = static_cast<uint32_t>(extent2D.height * pow(0.5f, m));

            commandBuffer.SetViewportAndScissor(mipExtent);

            auto beginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer);
            commandBuffer.BeginRenderPass(beginInfo);

            mat4 viewProjection = glm::perspective((float)(PI / 2.0), 1.0f, 0.1f, 512.0f) * _mvpMatrices[layer];
            commandBuffer.PushConstants(shader, 0, viewProjection);

            _prefilterEnv.Roughness = (float)m / (float)(mipLevels - 1);
            commandBuffer.PushConstants(shader, 1, _prefilterEnv);

            commandBuffer.BindPipeline(_prefilteredPipeline);

            auto prefilteredBuilder = renderFrame.GetResources().CreateDescriptorSetBuilder(shader, 0);
            prefilteredBuilder.SetTextureBuffer(0, _skyCubemap);
            auto& prefilteredResources = prefilteredBuilder.Build();
            commandBuffer.BindDescriptorSet(
                _prefilteredPipeline->GetPipelineBindPoint(),
                shader, prefilteredResources);

            commandBuffer.BindVertexBuffers(_prefilteredVertexBuffers, 0);
            commandBuffer.BindIndexBuffer(*_skyIndexBuffer, _skyIndexType);
            commandBuffer.DrawIndexed(_sky->GetIndexCount(), 1);

            commandBuffer.EndRenderPass();

            commandBuffer.CreateBarrierBatch()
                .Image(*_colorRenderTarget,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
                .Submit();

            commandBuffer.CopyImage(*_colorRenderTarget, 
                *_prefilteredCubemap, 0, 0, m, layer);

            commandBuffer.CreateBarrierBatch()
                .Image(*_colorRenderTarget,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .Submit();
        }
    }

    commandBuffer.CreateBarrierBatch()
        .Image(*_prefilteredCubemap,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}

Core::PreEnvironmentJob::PreEnvironmentJob(Device& device, PreEnvironmentPass& pass)
    : Job(JobType::GRAPHICS_PRIMARY)
    , _pass(pass)
    , _tempRenderFrame(device)
{
    _pass.Initialize();
}

Core::PreEnvironmentJob::~PreEnvironmentJob()
{
}

void Core::PreEnvironmentJob::Execute()
{
    _pass.Draw(_tempRenderFrame, *commandBuffer, 0);

    status = JobStatus::COMPLETE;
}
