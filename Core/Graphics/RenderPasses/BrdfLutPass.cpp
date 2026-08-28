#include "stdafx.h"
#include "BrdfLutPass.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceManager.h"

using namespace Core;

Core::BrdfLutPass::BrdfLutPass(Device& device, ResourceManager& resourceManager, VkFormat lutFormat)
    : _device(device)
    , _resourceManager(resourceManager)
    , _lutFormat(lutFormat)
    , _pipelineState(make_unique<PipelineState>())
{
}

Core::BrdfLutPass::~BrdfLutPass()
{
    delete(_brdfMaterial);
}

void Core::BrdfLutPass::Initialize()
{
    auto shaderHandle = _resourceManager.LoadShader("BRDF");
    _brdfMaterial = new Material(_device, _resourceManager, shaderHandle, "brdf lut");

    auto pipelineState = *_pipelineState;

    auto& rasterization = pipelineState.GetRasterizationStateCreateInfo();
    rasterization.cullMode = VK_CULL_MODE_NONE;

    auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
    depthInfo.depthWriteEnable = VK_FALSE;
    depthInfo.depthTestEnable = VK_FALSE;

    PipelineRenderingDesc renderingDesc;
    renderingDesc.colorFormats = { _lutFormat };

    _brdfPipeline = make_unique<Pipeline>(_device,
        renderingDesc, _brdfMaterial->GetShaderHandle().Get(), pipelineState);
}

void Core::BrdfLutPass::Record(CommandBuffer& commandBuffer, Texture& brdfLut)
{
    commandBuffer.CreateBarrierBatch()
        .Image(brdfLut,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        .Submit();

    auto extent = brdfLut.GetExtent();
    VkExtent2D extent2D = { extent.width, extent.height };

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = brdfLut.GetImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

    RenderingSetup setup;
    setup.renderArea = extent2D;
    setup.colorAttachments.push_back(colorAttachment);

    commandBuffer.SetViewportAndScissor(extent2D);
    commandBuffer.BeginRendering(setup);

    commandBuffer.BindPipeline(_brdfPipeline.get());
    commandBuffer.Draw(3, 1);

    commandBuffer.EndRendering();

    commandBuffer.CreateBarrierBatch()
        .Image(brdfLut,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}
