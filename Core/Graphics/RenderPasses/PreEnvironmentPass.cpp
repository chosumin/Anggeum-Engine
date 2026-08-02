#include "stdafx.h"
#include "PreEnvironmentPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceManager.h"
#include "Utils/Utility.h"

using namespace Core;

#define PI 3.1415926535897932384626433832795

Core::PreEnvironmentPass::PreEnvironmentPass(Device& device, Scene& scene, VkFormat offscreenFormat)
    : _device(device)
    , _scene(scene)
    , _pipelineState(make_unique<PipelineState>())
    , _offscreenFormat(offscreenFormat)
    , _irradianceShader(&device.GetResourceManager().LoadMaterial("irradiance", "Irradiance").Get().GetShaderHandle().Get())
    , _prefilteredShader(&device.GetResourceManager().LoadMaterial("prefiltered", "Prefiltered").Get().GetShaderHandle().Get())
{
}

Core::PreEnvironmentPass::~PreEnvironmentPass() = default;

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

        PipelineRenderingDesc renderingDesc;
        renderingDesc.colorFormats = { _offscreenFormat };

        _irradiancePipeline = make_unique<Pipeline>(_device, renderingDesc, *_irradianceShader, pipelineState);
        _prefilteredPipeline = make_unique<Pipeline>(_device, renderingDesc, *_prefilteredShader, pipelineState);

        // Resolve the sky's vertex/index buffers here (main thread) so Record() on
        // the worker thread binds raw pointers instead of resolving pool handles.
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

void Core::PreEnvironmentPass::Record(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    Texture& offscreen, Texture& irradiance, Texture& prefiltered)
{
    if (_sky == nullptr)
        return;

    RecordIrradiance(context, commandBuffer, offscreen, irradiance);
    RecordPrefiltered(context, commandBuffer, offscreen, prefiltered);
}

void Core::PreEnvironmentPass::BeginOffscreenRendering(CommandBuffer& commandBuffer, VkExtent2D extent,
    Texture& offscreen)
{
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = offscreen.GetImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

    RenderingSetup setup;
    setup.renderArea = extent;
    setup.colorAttachments.push_back(colorAttachment);

    commandBuffer.BeginRendering(setup);
}

void Core::PreEnvironmentPass::RecordIrradiance(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    Texture& offscreen, Texture& irradiance)
{
    auto& shader = *_irradianceShader;

    commandBuffer.CreateBarrierBatch()
        .Image(irradiance,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        .Image(offscreen,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        .Submit();

    uint32_t mipLevels = irradiance.GetMipLevels();
    uint32_t layers = irradiance.GetLayers();

    auto extent = offscreen.GetExtent();
    VkExtent2D extent2D = { extent.width, extent.height };

    for (uint32_t m = 0; m < mipLevels; ++m)
    {
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            VkExtent2D mipExtent;
            mipExtent.width = static_cast<uint32_t>(extent2D.width * pow(0.5f, m));
            mipExtent.height = static_cast<uint32_t>(extent2D.height * pow(0.5f, m));

            commandBuffer.SetViewportAndScissor(mipExtent);

            BeginOffscreenRendering(commandBuffer, mipExtent, offscreen);

            mat4 viewProjection = glm::perspective((float)(PI / 2.0), 1.0f, 0.1f, 512.0f) * _mvpMatrices[layer];
            commandBuffer.PushConstants(shader, 0, viewProjection);
            commandBuffer.PushConstants(shader, 1, _delta);

            commandBuffer.BindPipeline(_irradiancePipeline.get());

            auto irradianceBuilder = context.CreateDescriptorSetBuilder(shader, 0);
            irradianceBuilder.SetTextureBuffer(0, _skyCubemap.Get());
            auto& irradianceResources = irradianceBuilder.Build();
            commandBuffer.BindDescriptorSet(
                _irradiancePipeline->GetPipelineBindPoint(),
                shader, irradianceResources);

            commandBuffer.BindVertexBuffers(_irradianceVertexBuffers, 0);
            commandBuffer.BindIndexBuffer(*_skyIndexBuffer, _skyIndexType);
            commandBuffer.DrawIndexed(_sky->GetIndexCount(), 1);

            commandBuffer.EndRendering();

            commandBuffer.CreateBarrierBatch()
                .Image(offscreen,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
                .Submit();

            commandBuffer.CopyImage(offscreen,
                irradiance, 0, 0, m, layer);

            commandBuffer.CreateBarrierBatch()
                .Image(offscreen,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .Submit();
        }
    }

    commandBuffer.CreateBarrierBatch()
        .Image(irradiance,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}

void Core::PreEnvironmentPass::RecordPrefiltered(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    Texture& offscreen, Texture& prefiltered)
{
    auto& shader = *_prefilteredShader;

    commandBuffer.CreateBarrierBatch()
        .Image(prefiltered,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        .Submit();

    uint32_t mipLevels = prefiltered.GetMipLevels();
    uint32_t layers = prefiltered.GetLayers();

    auto extent = offscreen.GetExtent();
    VkExtent2D extent2D = { extent.width, extent.height };

    for (uint32_t m = 0; m < mipLevels; ++m)
    {
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            VkExtent2D mipExtent;
            mipExtent.width = static_cast<uint32_t>(extent2D.width * pow(0.5f, m));
            mipExtent.height = static_cast<uint32_t>(extent2D.height * pow(0.5f, m));

            commandBuffer.SetViewportAndScissor(mipExtent);

            BeginOffscreenRendering(commandBuffer, mipExtent, offscreen);

            mat4 viewProjection = glm::perspective((float)(PI / 2.0), 1.0f, 0.1f, 512.0f) * _mvpMatrices[layer];
            commandBuffer.PushConstants(shader, 0, viewProjection);

            _prefilterEnv.Roughness = (float)m / (float)(mipLevels - 1);
            commandBuffer.PushConstants(shader, 1, _prefilterEnv);

            commandBuffer.BindPipeline(_prefilteredPipeline.get());

            auto prefilteredBuilder = context.CreateDescriptorSetBuilder(shader, 0);
            prefilteredBuilder.SetTextureBuffer(0, _skyCubemap.Get());
            auto& prefilteredResources = prefilteredBuilder.Build();
            commandBuffer.BindDescriptorSet(
                _prefilteredPipeline->GetPipelineBindPoint(),
                shader, prefilteredResources);

            commandBuffer.BindVertexBuffers(_prefilteredVertexBuffers, 0);
            commandBuffer.BindIndexBuffer(*_skyIndexBuffer, _skyIndexType);
            commandBuffer.DrawIndexed(_sky->GetIndexCount(), 1);

            commandBuffer.EndRendering();

            commandBuffer.CreateBarrierBatch()
                .Image(offscreen,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
                .Submit();

            commandBuffer.CopyImage(offscreen,
                prefiltered, 0, 0, m, layer);

            commandBuffer.CreateBarrierBatch()
                .Image(offscreen,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .Submit();
        }
    }

    commandBuffer.CreateBarrierBatch()
        .Image(prefiltered,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}
