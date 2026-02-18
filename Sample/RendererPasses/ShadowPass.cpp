#include "stdafx.h"
#include "ShadowPass.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"

using namespace Core;

Core::ShadowPass::ShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
    Scene& scene, SwapChain& swapChain, VkFormat depthFormat, TransformBatch& transformBatch)
    : RendererPass(device, workerThreadManager)
	, _scene(scene), _msaaSamples(VK_SAMPLE_COUNT_1_BIT)
{
    auto swapChainExtent = swapChain.GetSwapChainExtent();
    _shadowExtent = { swapChainExtent.width, swapChainExtent.height };

    _renderPass->CreateDepthAttachment(depthFormat, _msaaSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
    _renderPass->CreateRenderPass();

    _shadowMaterial = _device.GetResourceCache().RequestMaterial("shadow", "Shadow");

    _rendererBatches = make_unique<RendererBatches>(device, transformBatch);

    auto meshes = _scene.GetComponents<Core::Mesh>();
    _rendererBatches->PrepareSingleBatch(_device,
        _shadowMaterial,
        *_renderPass, *_pipelineState, meshes);

    if (_device.IsGpuDrivenRenderingEnabled())
        _rendererBatches->PrepareGPUDrivenRendering(_device, false, swapChainExtent);
}

Core::ShadowPass::~ShadowPass()
{
}

void Core::ShadowPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc depthDesc{};
    depthDesc.extent = _shadowExtent;
    depthDesc.format = VK_FORMAT_UNDEFINED;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

    renderFrame.GetOrCreateRenderTarget(RT_SHADOW_DEPTH, depthDesc);
}

void Core::ShadowPass::Prepare()
{
}

void Core::ShadowPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
    EnsureRenderTargets(renderFrame);

    auto* framebuffer = renderFrame.GetOrCreateFramebuffer(
        "ShadowPass",
        *_renderPass,
        { RT_SHADOW_DEPTH });

    if (!framebuffer)
        return;

    auto& commandBuffer = renderFrame.GetCommandBuffer();

    // Update shadow matrices
    auto lights = _scene.GetComponents<Light>();
    if (!lights.empty())
    {
        auto light = lights[0];
        auto& transform = light->GetEntity().GetTransform();
        
        // Calculate light view matrix
        glm::mat4 lightMatrix = transform.GetMatrix();
        glm::vec3 lightPosition = glm::vec3(lightMatrix[3]);
        glm::vec3 lightDirection = glm::normalize(glm::vec3(lightMatrix[2]));
        glm::vec3 lightUp = glm::normalize(glm::vec3(lightMatrix[1]));
        
        _directionalLight.View = glm::lookAt(lightPosition, lightPosition + lightDirection, lightUp);
        
        // Calculate orthographic projection for directional light shadow
        float shadowSize = 50.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
        _directionalLight.Projection = glm::ortho(-shadowSize, shadowSize, -shadowSize, shadowSize, nearPlane, farPlane);
        
        // Calculate shadow buffer projection (View * Projection)
        _shadowBuffer.Projection = _directionalLight.Projection * _directionalLight.View;
    }

    commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

    auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*framebuffer);
    commandBuffer.BeginRenderPass(renderPassBeginInfo);

    if (_device.IsGpuDrivenRenderingEnabled())
    {
        _rendererBatches->DrawIndirect(renderFrame, commandBuffer,
        [&](shared_ptr<Shader> shader)
        {
            renderFrame.SetShaderUniformBuffer(*shader, 0, &_directionalLight);
        },
        [&](shared_ptr<Material> sharedMaterial)
        {
        });
    }
    else
    {
        _rendererBatches->Draw(renderFrame, commandBuffer,
        [&](shared_ptr<Shader> shader)
        {
            renderFrame.SetShaderUniformBuffer(*shader, 0, &_directionalLight);
        },
        [&](shared_ptr<Material> sharedMaterial, shared_ptr<SubMesh> subMesh)
        {
        });
    }

    commandBuffer.EndRenderPass();
}
