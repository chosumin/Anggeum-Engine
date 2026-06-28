#include "stdafx.h"
#include "GeometryPass.h"
#include "DFAOPass.h"
#include "CACAOPass.h"
#include "SDFShadowPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Component.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/RenderContext.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "Graphics/ResourceCache.h"
#include "PreEnvironmentPass.h"
#include "BrdfLutPass.h"

namespace Core
{
    GeometryPass::GeometryPass(Device& device, WorkerThreadManager& workerThreadManager,
        Scene& scene, SwapChain& swapChain, VkFormat depthFormat,
        VkSampleCountFlagBits msaaSamples, ShadowUniform& shadowBuffer,
        Buffer* lightVisibilityBuffer, ivec2 tileNums,
        TransformBatch& transformBatch)
        : RendererPass(device, workerThreadManager)
        , _scene(scene)
        , _msaaSamples(msaaSamples)
        , _swapChainFormat(swapChain.GetImageFormat())
        , _lightVisibilityBuffer(lightVisibilityBuffer)
		, _shadowBuffer(shadowBuffer)
    {
        auto swapChainExtents = swapChain.GetSwapChainExtent();
        _tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
        _tileInfo.tileNums = tileNums;

        auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
        multiSampling.rasterizationSamples = msaaSamples;

        auto& depthStencil = _pipelineState->GetDepthStencilStateCreateInfo();
        depthStencil.depthWriteEnable = VK_FALSE;

        // Pass 1: color CLEAR, depth LOAD
        _renderPass->CreateColorAttachment(swapChain.GetImageFormat(), _msaaSamples,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
        _renderPass->CreateDepthAttachment(depthFormat, _msaaSamples,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
        _renderPass->CreateRenderPass();

        // Pass 2: color LOAD, depth LOAD (preserves Pass 1 results)
        _renderPassPass2 = new RenderPass(_device);
        _renderPassPass2->CreateColorAttachment(swapChain.GetImageFormat(), _msaaSamples,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
        _renderPassPass2->CreateDepthAttachment(depthFormat, _msaaSamples,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
        _renderPassPass2->CreateRenderPass();

        _rendererBatches = make_unique<RendererBatches>(device, transformBatch);

        auto meshes = _scene.GetComponents<Core::Mesh>();
        _rendererBatches->Prepare(_device, *_renderPass, *_pipelineState, meshes);

        if (_device.IsGpuDrivenRenderingEnabled())
        {
            _rendererBatches->PrepareGPUDrivenRendering(_device, true, swapChainExtents);
        }
    }

    GeometryPass::~GeometryPass()
    {
        delete(_skyboxPipeline);
        delete(_renderPassPass2);
    }

    void GeometryPass::EnsureRenderTargets(RenderFrame& renderFrame)
    {
        VkExtent2D screenExtent = { 
            static_cast<uint32_t>(_tileInfo.viewportSize.x), 
            static_cast<uint32_t>(_tileInfo.viewportSize.y) 
        };

        // Main color target
        RenderTargetDesc colorDesc{};
        colorDesc.extent = screenExtent;
        colorDesc.format = _swapChainFormat;
        colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        colorDesc.samples = _msaaSamples;
        colorDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        renderFrame.GetOrCreateRenderTarget(RT_MAIN_COLOR, colorDesc);

        // Main depth target
        RenderTargetDesc depthDesc{};
        depthDesc.extent = screenExtent;
        depthDesc.format = VK_FORMAT_UNDEFINED;
        depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        depthDesc.samples = _msaaSamples;
        depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        renderFrame.GetOrCreateRenderTarget(RT_MAIN_DEPTH, depthDesc);
    }

    void GeometryPass::EnsureIBLResources(RenderFrame& renderFrame)
    {
        // Create only once since these are read only
        if (_offscreenTexture)
            return;

        // Offscreen (for IBL generation)
        RenderTargetDesc offscreenDesc{};
        offscreenDesc.extent = { 128, 128 };
        offscreenDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        offscreenDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        offscreenDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        offscreenDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        _offscreenTexture = renderFrame.GetOrCreateRenderTarget(RT_OFFSCREEN, offscreenDesc);

        // Irradiance cubemap
        RenderTargetDesc irradianceDesc{};
        irradianceDesc.extent = { 128, 128 };
        irradianceDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        irradianceDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        irradianceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        irradianceDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        irradianceDesc.isCubemap = true;
        irradianceDesc.mipLevels = 8;
        irradianceDesc.arrayLayers = 6;
        _irradianceCubemap = renderFrame.GetOrCreateRenderTarget(RT_IRRADIANCE, irradianceDesc);

        // Prefiltered cubemap
        RenderTargetDesc prefilteredDesc{};
        prefilteredDesc.extent = { 128, 128 };
        prefilteredDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        prefilteredDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        prefilteredDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        prefilteredDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        prefilteredDesc.isCubemap = true;
        prefilteredDesc.mipLevels = 8;
        prefilteredDesc.arrayLayers = 6;
        _prefilteredCubemap = renderFrame.GetOrCreateRenderTarget(RT_PREFILTERED, prefilteredDesc);

        // BRDF LUT
        RenderTargetDesc brdfLutDesc{};
        brdfLutDesc.extent = { 512, 512 };
        brdfLutDesc.format = VK_FORMAT_R16G16_SFLOAT;
        brdfLutDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        brdfLutDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        brdfLutDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        _brdfLut = renderFrame.GetOrCreateRenderTarget(RT_BRDF_LUT, brdfLutDesc);
    }

    void GeometryPass::Prepare()
    {
    }

    void GeometryPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
    {
        // Lazy initialization
        EnsureRenderTargets(renderFrame);
        EnsureIBLResources(renderFrame);

        if (!_iblGenerated)
        {
            PreparePregenerationSkybox(renderFrame);
            RegisterGiTexturesToBindless(renderFrame);
            _iblGenerated = true;
        }

        auto* framebuffer = renderFrame.GetOrCreateFramebuffer(
            "GeometryPass",
            *_renderPass,
            { RT_MAIN_COLOR, RT_MAIN_DEPTH });

        if (!framebuffer)
            return;

        auto& commandBuffer = renderFrame.GetCommandBuffer();
        PerspectiveCamera* camera = _scene.GetMainCamera();
        auto shadowTarget    = renderFrame.GetRenderTarget(RT_SHADOW_DEPTH);
        auto sdfShadowTarget = renderFrame.GetRenderTarget("SDFShadow");

        auto dfaoTarget = renderFrame.GetRenderTarget(DFAOPass::RT_DFAO);

        UpdateLightBuffer();

        if (shadowTarget)
        {
            commandBuffer.TransitionImageLayout(*shadowTarget->GetImage().lock(),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

        auto perShader = [&](shared_ptr<Shader> shader)
        {
            renderFrame.SetShaderUniformBuffer(*shader, 0, &camera->Matrices);
            renderFrame.SetShaderUniformBuffer(*shader, 3, &_giBuffer);
            renderFrame.SetShaderUniformBuffer(*shader, 4, &_shadowBuffer);
            renderFrame.SetShaderUniformBuffer(*shader, 5, &_lightBuffer);
            renderFrame.SetShaderStorageBuffer(*shader, 6, _lightVisibilityBuffer);
            renderFrame.SetShaderTextureBuffer(*shader, 7, shadowTarget);

            if (sdfShadowTarget)
                renderFrame.SetShaderTextureBuffer(*shader, 10, sdfShadowTarget);

            if (dfaoTarget)
               renderFrame.SetShaderTextureBuffer(*shader, 11, dfaoTarget);
        };

        auto perDraw = [&](shared_ptr<Material> sharedMaterial)
        {
            sharedMaterial->SetPushConstants<TileInfo>(_tileInfo);
            commandBuffer.PushConstants(*sharedMaterial, 0);
        };

        if (_device.IsGpuDrivenRenderingEnabled())
        {
            auto previousDepth = renderFrame.GetPreviousDepthBuffer();
            auto depthTarget = renderFrame.GetRenderTarget(RT_MAIN_DEPTH);

            _rendererBatches->GpuDrivenDraw(
                renderFrame, commandBuffer,
                previousDepth, depthTarget,
                camera->Matrices,
                *_renderPass, *_renderPassPass2,
                *framebuffer,
                perShader, perDraw,
                [&]() { DrawSkybox(renderFrame, commandBuffer); });
        }
        else
        {
            auto renderPassBeginInfo =
                _renderPass->CreateRenderPassBeginInfo(*framebuffer);
            commandBuffer.BeginRenderPass(renderPassBeginInfo);

            _rendererBatches->Draw(renderFrame, commandBuffer, perShader,
                [&](shared_ptr<Material> sharedMaterial, shared_ptr<SubMesh> subMesh)
                {
                    sharedMaterial->SetPushConstants<TileInfo>(_tileInfo);
                    commandBuffer.PushConstants(*sharedMaterial, 0);
                });

            DrawSkybox(renderFrame, commandBuffer);
            commandBuffer.EndRenderPass();
        }
    }

    void GeometryPass::PreparePregenerationSkybox(RenderFrame& renderFrame)
    {
        _timer.tick();

        auto preEnvironmentPass = new PreEnvironmentPass(_device, _workerThreadManager, _scene, 
            _offscreenTexture.get(), _irradianceCubemap.get(), _prefilteredCubemap.get());
        auto preEnvironmentJob = new PreEnvironmentJob(_device, *preEnvironmentPass);
        Enqueue(preEnvironmentJob);

        auto brdf = new BrdfLutPass(_device, _workerThreadManager, _brdfLut.get());
        auto brdfJob = new BrdfLutJob(_device, *brdf);
        Enqueue(brdfJob);

        Wait();

        const size_t commandSize = 2;
        vector<VkCommandBuffer> commands(commandSize);
        commands[0] = preEnvironmentJob->commandBuffer->GetHandle();
        commands[1] = brdfJob->commandBuffer->GetHandle();

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = commandSize;
        submitInfo.pCommandBuffers = commands.data();

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = 0;

        VkFence fence;
        vkCreateFence(_device.GetDevice(), &fence_info, nullptr, &fence);

        vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, fence);
        
        auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
        std::cout << "Generation IBL resources time : " << deltaTime << endl;

        vkWaitForFences(_device.GetDevice(), 1, &fence, VK_TRUE, 100000000000);

        vkDestroyFence(_device.GetDevice(), fence, nullptr);

        delete(brdf);
        delete(brdfJob);
        delete(preEnvironmentPass);
        delete(preEnvironmentJob);
    }

    void GeometryPass::RegisterGiTexturesToBindless(RenderFrame& renderFrame)
    {
        if (!renderFrame.HasBindlessSupport())
            return;

        if (!_irradianceCubemap || !_prefilteredCubemap || !_brdfLut)
            return;

        auto* bindlessManager = renderFrame.GetBindlessTextureManager();

        TextureHandle irradianceCubemapHandle = bindlessManager->RegisterTexture(_irradianceCubemap);
        TextureHandle prefilteredCubemapHandle = bindlessManager->RegisterTexture(_prefilteredCubemap);
        TextureHandle brdfLutHandle = bindlessManager->RegisterTexture(_brdfLut);

        // Strip the MSB cubemap flag before passing to the shader.
        // handle.index stores 0x80000000 as a cubemap marker internally,
        // but the shader uses the value as a direct array index (no flags expected).
        _giBuffer.irradianceMapIndex = irradianceCubemapHandle.index & 0x7FFFFFFF;
        _giBuffer.prefilterMapIndex  = prefilteredCubemapHandle.index & 0x7FFFFFFF;
        _giBuffer.brdfLUTIndex       = brdfLutHandle.index & 0x7FFFFFFF;

        cout << "GI textures registered to bindless:" << endl;
        cout << "  Irradiance cubemap: index " << _giBuffer.irradianceMapIndex << endl;
        cout << "  Prefiltered cubemap: index " << _giBuffer.prefilterMapIndex << endl;
        cout << "  BRDF LUT: index " << _giBuffer.brdfLUTIndex << endl;
    }

    void GeometryPass::DrawSkybox(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
    {
        PerspectiveCamera* camera = _scene.GetMainCamera();

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
            auto material = skybox->GetMaterials()[0];
            auto subMesh = skybox->GetSubMeshes()[0];
            auto& shader = material->GetShader();

            if (_skyboxPipeline == nullptr)
            {
                auto pipelineState = *_pipelineState;
                auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
                depthInfo.depthWriteEnable = VK_FALSE;

                auto& rasterizationInfo = pipelineState.GetRasterizationStateCreateInfo();
                rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;

                _skyboxPipeline = new Pipeline(_device, *_renderPass, shader, pipelineState);
            }

            renderFrame.SetShaderUniformBuffer(shader, 0, &camera->Matrices);

            commandBuffer.BindPipeline(_skyboxPipeline);

            commandBuffer.BindDescriptorSets(
                renderFrame,
                _skyboxPipeline->GetPipelineBindPoint(), material->GetShader());
            commandBuffer.BindDescriptorSets(
                renderFrame,
                _skyboxPipeline->GetPipelineBindPoint(), *material);

            auto vertexAttibuteNames = material->GetShader().GetVertexAttirbuteNames();

            commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);

            commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());

            commandBuffer.DrawIndexed(subMesh->GetIndexCount(), 1);
        }
    }

    void GeometryPass::UpdateLightBuffer()
    {
        auto lights = _scene.GetComponents<Light>();

        uint32_t size = std::min((uint32_t)lights.size(), (uint32_t)MAX_FORWARD_LIGHT_COUNT);
        for (uint32_t i = 0; i < size; ++i)
        {
            auto light = lights[i];

            auto& properties = light->GetProperties();
            auto& transform = light->GetEntity().GetTransform();

            LightInfo lightInfo{};
            lightInfo.Position = vec4(transform.GetTranslation(),
                static_cast<float>(light->GetLightType()));
            lightInfo.Color = vec4(properties.Color, properties.Intensity);

            auto direction = transform.GetRotation() * properties.Direction;
            lightInfo.Direction =
                vec4(direction, properties.Range);
            lightInfo.Info = vec2(properties.InnerConeAngle, properties.OuterConeAngle);

            _lightBuffer.Light[i] = lightInfo;
        }

        _lightBuffer.Count = size;
    }
}
