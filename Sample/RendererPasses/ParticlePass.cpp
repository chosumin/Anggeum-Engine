#include "stdafx.h"
#include "ParticlePass.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/SubMesh.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/TransferJob.h"
#include "Graphics/Material.h"
#include "Components/Mesh.h"
#include "Foundation/Scene.h"

#define PARTICLE_COUNT 8192

using namespace Core;

Sample::ParticlePass::ParticlePass(Device& device, WorkerThreadManager& workerThreadManager,
    Scene& scene, VkExtent2D extent, VkFormat swapChainFormat,
    VkSampleCountFlagBits msaaSamples)
    : RendererPass(device, workerThreadManager)
    , _scene(scene)
    , _extent(extent)
    , _swapChainFormat(swapChainFormat)
    , _msaaSamples(msaaSamples)
{
    _computeMaterial = device.GetResourceCache().RequestMaterial("particle", "shaders/particle.comp.spv");
    _computePipeline = make_unique<Pipeline>(device, _computeMaterial->GetShader());

    _graphicsMaterial =
        make_shared<Material>(_device, "particleGraphics", "shaders/particle.vert", "shaders/particle.frag");

    auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
    multiSampling.rasterizationSamples = msaaSamples;

    auto& inputAssembly = _pipelineState->GetInputAssemblyStateCreateInfo();
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    _renderPass->CreateColorAttachment(_swapChainFormat, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
    _renderPass->CreateRenderPass();

    _graphicsPipeline = make_unique<Pipeline>(_device, *_renderPass,
        _graphicsMaterial->GetShader(), *_pipelineState);
}

Sample::ParticlePass::~ParticlePass()
{
    for (auto& buffer : _buffers)
    {
        delete buffer;
    }
    _buffers.clear();
}

void Sample::ParticlePass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc colorDesc{};
    colorDesc.extent = _extent;
    colorDesc.format = _swapChainFormat;
    colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    colorDesc.samples = _msaaSamples;
    colorDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    renderFrame.GetOrCreateRenderTarget(RT_MAIN_COLOR, colorDesc);
}

void Sample::ParticlePass::Initialize()
{
    _buffers.resize(MAX_FRAMES_IN_FLIGHT * 3);

    vector<vec2> positions(PARTICLE_COUNT);
    vector<vec2> velocities(PARTICLE_COUNT);
    vector<vec4> colors(PARTICLE_COUNT);

    default_random_engine rndEngine((unsigned)time(nullptr));
    uniform_real_distribution<float> rndDist(0.0f, 1.0f);

    for (size_t i = 0; i < PARTICLE_COUNT; ++i)
    {
        float r = 0.25f * sqrt(rndDist(rndEngine));
        float theta = rndDist(rndEngine) * 2 * 3.14159265358979323846;
        float x = r * cos(theta) * _extent.height / _extent.width;
        float y = r * sin(theta);

        positions[i] = glm::vec2(x, y);
        velocities[i] = glm::normalize(glm::vec2(x, y)) * 0.00025f;
        colors[i] = glm::vec4(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine), 1.0f);
    }

    size_t positionByteSize = PARTICLE_COUNT * sizeof(vec2);
    vector<uint8_t> positionBytes(positionByteSize);
    memcpy(positionBytes.data(), positions.data(), positionByteSize);

    size_t velocityByteSize = PARTICLE_COUNT * sizeof(vec2);
    vector<uint8_t> velocityBytes(velocityByteSize);
    memcpy(velocityBytes.data(), velocities.data(), velocityByteSize);

    size_t colorByteSize = PARTICLE_COUNT * sizeof(vec4);
    vector<uint8_t> colorBytes(colorByteSize);
    memcpy(colorBytes.data(), colors.data(), colorByteSize);

    vector<Job*> jobs;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        jobs.push_back(new VkBufferJob(_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &_buffers[0 + 3 * i], positionBytes, true));
        jobs.push_back(new VkBufferJob(_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &_buffers[1 + 3 * i], velocityBytes, true));
        jobs.push_back(new VkBufferJob(_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &_buffers[2 + 3 * i], colorBytes, true));
    }

    CommandBuffer::ImmediateSubmit(_device, jobs);

    for (auto& job : jobs)
    {
        delete(job);
    }
}

void Sample::ParticlePass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
    auto* framebuffer = renderFrame.GetOrCreateFramebuffer(
        "ParticlePass",
        *_renderPass,
        { RT_MAIN_COLOR });

    if (!framebuffer)
        return;

    auto& commandBuffer = renderFrame.GetComputeCommandBuffer();

    // Compute pass
    _deltaTime.deltaTime += 0.01f;
    renderFrame.SetShaderUniformBuffer(_computeMaterial->GetShader(), 0, &_deltaTime.deltaTime);

    renderFrame.SetShaderStorageBuffer(_computeMaterial->GetShader(), 1, _buffers[0]);
    renderFrame.SetShaderStorageBuffer(_computeMaterial->GetShader(), 2, _buffers[1]);
    renderFrame.SetShaderStorageBuffer(_computeMaterial->GetShader(), 3, _buffers[3]);
    renderFrame.SetShaderStorageBuffer(_computeMaterial->GetShader(), 4, _buffers[4]);

    commandBuffer.BindPipeline(_computePipeline.get());

    commandBuffer.BindDescriptorSets(
        renderFrame,
        _computePipeline->GetPipelineBindPoint(), _computeMaterial->GetShader());

    commandBuffer.Dispatch(PARTICLE_COUNT / 256, 1, 1);

    // Graphics pass
    commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

    auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*framebuffer);
    commandBuffer.BeginRenderPass(renderPassBeginInfo);

    commandBuffer.BindPipeline(_graphicsPipeline.get());

    vector<Buffer*> vertexBuffers(2);
    vertexBuffers[0] = _buffers[0];
    vertexBuffers[1] = _buffers[2];

    commandBuffer.BindVertexBuffers(vertexBuffers, 0);
    commandBuffer.Draw(PARTICLE_COUNT, 1);

    commandBuffer.EndRenderPass();
}
