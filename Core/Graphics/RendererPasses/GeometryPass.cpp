#include "stdafx.h"
#include "GeometryPass.h"
#include "AmbientOcclusionPass.h"
#include "SDFShadowPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Component.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/RenderContext.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/RendererBatch.h"
#include "PreEnvironmentPass.h"
#include "BrdfLutPass.h"

namespace Core
{
	GeometryPass::GeometryPass(Device& device, WorkerThreadManager& workerThreadManager,
		Scene& scene, SwapChain& swapChain, VkFormat depthFormat,
		VkSampleCountFlagBits msaaSamples, ivec2 tileNums)
		: RendererPass(device, workerThreadManager)
		, _scene(scene)
		, _msaaSamples(msaaSamples)
		, _swapChainFormat(swapChain.GetImageFormat())
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
	}

    GeometryPass::~GeometryPass()
    {
        delete(_skyboxPipeline);
        delete(_renderPassPass2);

        for (auto& [shader, pipeline] : _pipelineCache)
        {
            delete(pipeline);
        }
    }

    void GeometryPass::EnsureRenderTargets(RenderFrame& renderFrame)
    {
        auto& frameResources = renderFrame.GetResources();
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
        frameResources.GetOrCreateRenderTarget(RT_MAIN_COLOR, colorDesc);

        // Main depth target
        RenderTargetDesc depthDesc{};
        depthDesc.extent = screenExtent;
        depthDesc.format = VK_FORMAT_UNDEFINED;
        depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        depthDesc.samples = _msaaSamples;
        depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        frameResources.GetOrCreateRenderTarget(RT_MAIN_DEPTH, depthDesc);
    }

    void GeometryPass::EnsureIBLResources(RenderFrame& renderFrame)
    {
        auto& frameResources = renderFrame.GetResources();
        // Create only once since these are read only
        if (_offscreenTexture.IsValid())
            return;

        // Offscreen (for IBL generation)
        RenderTargetDesc offscreenDesc{};
        offscreenDesc.extent = { 128, 128 };
        offscreenDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        offscreenDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        offscreenDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        offscreenDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        _offscreenTexture = frameResources.GetOrCreateRenderTarget(RT_OFFSCREEN, offscreenDesc);

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
        _irradianceCubemap = frameResources.GetOrCreateRenderTarget(RT_IRRADIANCE, irradianceDesc);

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
        _prefilteredCubemap = frameResources.GetOrCreateRenderTarget(RT_PREFILTERED, prefilteredDesc);

        // BRDF LUT
        RenderTargetDesc brdfLutDesc{};
        brdfLutDesc.extent = { 512, 512 };
        brdfLutDesc.format = VK_FORMAT_R16G16_SFLOAT;
        brdfLutDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        brdfLutDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        brdfLutDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        _brdfLut = frameResources.GetOrCreateRenderTarget(RT_BRDF_LUT, brdfLutDesc);
    }

    void GeometryPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        auto& frameResources = renderFrame.GetResources();
        // Wait for compute queue (AmbientOcclusionPass) to finish producing the AO texture
        renderFrame.GetCurrentSubmitInfo().AddWaitSemaphore(
            QueueType::Compute,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        // Produced by LightCullingPass into this frame's own buffer.
        BufferDesc lightVisibilityDesc{};
        lightVisibilityDesc.size = GetLightVisibilityBufferSize(_tileInfo.tileNums);
        auto& lightVisibilityBuffer =
            frameResources.GetOrCreateStorageBuffer(SB_LIGHT_VISIBILITY, lightVisibilityDesc).Get();

		commandBuffer.CreateBarrierBatch()
			.Buffer(lightVisibilityBuffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT)
			.Submit();

        // Lazy initialization
        EnsureIBLResources(renderFrame);

        if (!_iblGenerated)
        {
            PreparePregenerationSkybox(renderFrame);
            RegisterGiTexturesToBindless(renderFrame);
            _iblGenerated = true;
        }

        auto* framebuffer = frameResources.GetOrCreateFramebuffer(
            "GeometryPass",
            *_renderPass,
            { RT_MAIN_COLOR, RT_MAIN_DEPTH });

        if (!framebuffer)
            return;
        PerspectiveCamera* camera = _scene.GetMainCamera();

        auto depth = frameResources.GetRenderTarget(RT_MAIN_DEPTH);
        auto shadowTarget    = frameResources.GetRenderTarget(RT_SHADOW_DEPTH);
        auto sdfShadowTarget = frameResources.GetRenderTarget("SDFShadow");
        auto aoTarget = frameResources.GetRenderTarget(AmbientOcclusionPass::RT_AO);

        // Depth becomes this pass's depth attachment; the shadow map (if present)
        // flips to shader-read. Both fold into a single pipeline barrier.
        auto barrierBatch = commandBuffer.CreateBarrierBatch();
        barrierBatch.Image(depth.Get(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        if (shadowTarget.IsValid())
        {
            barrierBatch.Image(shadowTarget.Get(),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        barrierBatch.Submit();

        commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

        // Get a geometry shader for rendering (use first mesh's material shader)
        auto meshes = _scene.GetComponents<Core::Mesh>();
        Shader* shader = nullptr;
        for (auto* mesh : meshes)
        {
            auto& materials = mesh->GetMaterials();
            for (auto& materialHandle : materials)
            {
                auto& materialShader = materialHandle.Get().GetShaderHandle().Get();
                if (materialShader.GetPass() == "Geometry")
                {
                    shader = &materialShader;
                    break;
                }
            }
            if (shader)
                break;
        }

        Pipeline* pipeline = GetOrCreatePipeline(*shader);

        auto& cameraBuffer = frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA).Get();
        auto& lightBuffer = frameResources.GetOrCreateUniformBuffer<LightBuffer>(UB_LIGHTS).Get();
        auto& shadowBuffer = frameResources.GetOrCreateUniformBuffer<ShadowUniform>(UB_SHADOW).Get();

        auto& giBuffer = frameResources.GetOrCreateUniformBuffer<GI>("GeometryPass.GI").Get();
        giBuffer.Update(_giBuffer);

        auto builder = frameResources.CreateDescriptorSetBuilder(*shader, 0);
        builder.SetUniformBuffer(0, cameraBuffer);
        builder.SetUniformBuffer(3, giBuffer);
        builder.SetUniformBuffer(4, shadowBuffer);
        builder.SetUniformBuffer(5, lightBuffer);
        builder.SetStorageBuffer(6, lightVisibilityBuffer);
        builder.SetTextureBuffer(7, shadowTarget);

        if (sdfShadowTarget.IsValid())
            builder.SetTextureBuffer(10, sdfShadowTarget);

        if (aoTarget.IsValid())
            builder.SetTextureBuffer(11, aoTarget);

        auto perShaderHook = [&](Shader& shader)
        {
            commandBuffer.PushConstants(shader, 0, _tileInfo);
        };

        auto& executor = renderFrame.GetRenderExecutor();
        executor.OcclusionCullAndDraw(
            commandBuffer,
            *shader, *pipeline,
            camera->Matrices,
            *_renderPass, *_renderPassPass2,
            *framebuffer,
            builder, perShaderHook,
            [&]() { DrawSkybox(renderFrame, commandBuffer); });
    }

    Pipeline* GeometryPass::GetOrCreatePipeline(Shader& shader)
    {
        auto it = _pipelineCache.find(&shader);
        if (it != _pipelineCache.end())
            return it->second;

        auto* pipeline = new Pipeline(_device, *_renderPass, shader, *_pipelineState);
        _pipelineCache[&shader] = pipeline;
        return pipeline;
    }

    void GeometryPass::PreparePregenerationSkybox(RenderFrame& renderFrame)
    {
        _timer.tick();

        auto preEnvironmentPass = new PreEnvironmentPass(_device, _workerThreadManager, _scene, 
            &_offscreenTexture.Get(), &_irradianceCubemap.Get(), &_prefilteredCubemap.Get());
        auto preEnvironmentJob = new PreEnvironmentJob(_device, *preEnvironmentPass);
        Enqueue(preEnvironmentJob);

        auto brdf = new BrdfLutPass(_device, _workerThreadManager, &_brdfLut.Get());
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
        auto& frameResources = renderFrame.GetResources();
        if (!renderFrame.HasBindlessSupport())
            return;

        if (!_irradianceCubemap.IsValid() || !_prefilteredCubemap.IsValid() || !_brdfLut.IsValid())
            return;

        auto* bindlessManager = renderFrame.GetBindlessTextureManager();

        uint32_t irradianceCubemapIndex = bindlessManager->RegisterTexture(frameResources.GetRenderTarget(RT_IRRADIANCE));
        uint32_t prefilteredCubemapIndex = bindlessManager->RegisterTexture(frameResources.GetRenderTarget(RT_PREFILTERED));
        uint32_t brdfLutIndex = bindlessManager->RegisterTexture(frameResources.GetRenderTarget(RT_BRDF_LUT));

        // Strip the MSB cubemap flag before passing to the shader.
        // The bindless index stores BindlessCubemapFlag as a cubemap marker internally,
        // but the shader uses the value as a direct array index (no flags expected).
        _giBuffer.irradianceMapIndex = irradianceCubemapIndex & ~BindlessCubemapFlag;
        _giBuffer.prefilterMapIndex  = prefilteredCubemapIndex & ~BindlessCubemapFlag;
        _giBuffer.brdfLUTIndex       = brdfLutIndex & ~BindlessCubemapFlag;

        std::cout << "GI textures registered to bindless:" << endl;
        std::cout << "  Irradiance cubemap: index " << _giBuffer.irradianceMapIndex << endl;
        std::cout << "  Prefiltered cubemap: index " << _giBuffer.prefilterMapIndex << endl;
        std::cout << "  BRDF LUT: index " << _giBuffer.brdfLUTIndex << endl;
    }

    void GeometryPass::DrawSkybox(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
    {
        auto& frameResources = renderFrame.GetResources();
        PerspectiveCamera* camera = _scene.GetMainCamera();

        auto meshes = _scene.GetComponents<Core::Mesh>();

        auto it = find_if(meshes.begin(), meshes.end(), [](Mesh* mesh)
        {
            auto& material = mesh->GetMaterials()[0].Get();
            auto& shader = material.GetShaderHandle().Get();
            return shader.GetPass() == "Skybox";
        });

        if (it != meshes.end())
        {
            auto skybox = *it;
            auto& material = skybox->GetMaterials()[0].Get();
            auto& subMesh = skybox->GetSubMeshes()[0].Get();
            auto& shader = material.GetShaderHandle().Get();

            if (_skyboxPipeline == nullptr)
            {
                auto pipelineState = *_pipelineState;
                auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
                depthInfo.depthWriteEnable = VK_FALSE;

                auto& rasterizationInfo = pipelineState.GetRasterizationStateCreateInfo();
                rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;

                _skyboxPipeline = new Pipeline(_device, *_renderPass, shader, pipelineState);
            }

            auto& skyCameraBuffer =
                frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA).Get();

            auto skyBuilder0 = frameResources.CreateDescriptorSetBuilder(shader, 0);
            skyBuilder0.SetUniformBuffer(0, skyCameraBuffer);
            auto& skyResources0 = skyBuilder0.Build();

            auto skyBuilder1 = frameResources.CreateDescriptorSetBuilder(shader, 1);
            auto& textures = material.GetTexturesMap();
            for (auto& [binding, texture] : textures)
                skyBuilder1.SetTextureBuffer(binding, texture);
            auto& skyResources1 = skyBuilder1.Build();

            commandBuffer.BindPipeline(_skyboxPipeline);

            commandBuffer.BindDescriptorSets(
                _skyboxPipeline->GetPipelineBindPoint(),
                shader, { &skyResources0, &skyResources1 });

            auto vertexAttibuteNames = shader.GetVertexAttirbuteNames();

            commandBuffer.BindVertexBuffers(subMesh.GetVertexBuffers(vertexAttibuteNames), 0);

            commandBuffer.BindIndexBuffer(subMesh.GetIndexBuffer(), subMesh.GetIndexType());

            commandBuffer.DrawIndexed(subMesh.GetIndexCount(), 1);
        }
    }

}
