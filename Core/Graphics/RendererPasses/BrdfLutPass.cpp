#include "stdafx.h"
#include "BrdfLutPass.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"

using namespace Core;

Core::BrdfLutPass::BrdfLutPass(Device& device, WorkerThreadManager& workerThreadManager,
    Texture* brdfLut)
    : RendererPass(device, workerThreadManager)
{
    _renderPass->CreateColorAttachment(brdfLut, VK_ATTACHMENT_LOAD_OP_CLEAR, 
        VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    _renderPass->CreateRenderPass();

    _framebuffer = make_unique<Framebuffer>(_device, *_renderPass, vector<Texture*>{ brdfLut });
}

Core::BrdfLutPass::~BrdfLutPass()
{
    delete(_brdfPipeline);
    delete(_brdfMaterial);
}

void Core::BrdfLutPass::Initialize()
{
    _brdfMaterial = new Material(_device, "BRDF", "brdf lut");

    auto pipelineState = *_pipelineState;

    auto& rasterization = pipelineState.GetRasterizationStateCreateInfo();
    rasterization.cullMode = VK_CULL_MODE_NONE;

    auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
    depthInfo.depthWriteEnable = VK_FALSE;
    depthInfo.depthTestEnable = VK_FALSE;

    _brdfPipeline = new Pipeline(_device, *_renderPass, _brdfMaterial->GetShader(), pipelineState);
}

void Core::BrdfLutPass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
    commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

    commandBuffer.BeginRenderPass(_renderPass->CreateRenderPassBeginInfo(*_framebuffer));

    commandBuffer.BindPipeline(_brdfPipeline);

    commandBuffer.Draw(3, 1);

    commandBuffer.EndRenderPass();
}

Core::BrdfLutJob::BrdfLutJob(Device& device, BrdfLutPass& pass)
    : Job(JobType::GRAPHICS_PRIMARY)
    , _pass(pass)
    , _tempRenderFrame(device)
{
    _pass.Initialize();
}

Core::BrdfLutJob::~BrdfLutJob()
{
}

void Core::BrdfLutJob::Execute()
{
    _pass.Draw(_tempRenderFrame, *commandBuffer, 0);

    status = JobStatus::COMPLETE;
}
