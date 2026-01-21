#include "stdafx.h"
#include "ParticlePass.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/SubMesh.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/TransferJob.h"
#include "Graphics/Material.h"
#include "Components/Mesh.h"
#include "Foundation/Scene.h"

#define PARTICLE_COUNT 8192

Sample::ParticlePass::ParticlePass(Core::Device& device, Core::WorkerThreadManager& workerThreadManager, Core::Scene& scene, Core::SwapChain& swapChain, shared_ptr<Core::Texture> colorRenderTarget)
	:Core::RendererPass(device, workerThreadManager), _scene(scene)
{
	_renderPass->CreateColorAttachment(colorRenderTarget.get(),
		VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
	_renderPass->CreateRenderPass();

	CreateFrameBuffer(swapChain);

	_computeMaterial = device.GetResourceCache().RequestMaterial("particle", "shaders/particle.comp");
	_computePipeline = make_unique<Core::Pipeline>(device, _computeMaterial->GetShader());

	_graphicsMaterial =
		make_shared<Core::Material>(_device, "particleGraphics", "shaders/particle.vert", "shaders/particle.frag");

	auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
	multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

	auto& inputAssembly = _pipelineState->GetInputAssemblyStateCreateInfo();
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

	_graphicsPipeline = make_unique<Core::Pipeline>(_device, *_renderPass, _graphicsMaterial->GetShader(), *_pipelineState);
}

Sample::ParticlePass::~ParticlePass()
{
	for (auto& buffer : _buffers)
	{
		delete buffer;
	}
	_buffers.clear();
}

void Sample::ParticlePass::Prepare()
{
	_buffers.resize(MAX_FRAMES_IN_FLIGHT * 3);

	vector<vec2> positions(PARTICLE_COUNT);
	vector<vec2> velocities(PARTICLE_COUNT);
	vector<vec4> colors(PARTICLE_COUNT);

	auto extent = _framebuffer->GetExtent();

	default_random_engine rndEngine((unsigned)time(nullptr));
	uniform_real_distribution<float> rndDist(0.0f, 1.0f);

	for (size_t i = 0; i < PARTICLE_COUNT; ++i)
	{
		float r = 0.25f * sqrt(rndDist(rndEngine));
		float theta = rndDist(rndEngine) * 2 * 3.14159265358979323846;
		float x = r * cos(theta) * extent.height / extent.width;
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

	vector<Core::Job*> jobs;
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		jobs.push_back(new Core::VkBufferJob(_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &_buffers[0 + 3 * i], positionBytes, true));
		jobs.push_back(new Core::VkBufferJob(_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &_buffers[1 + 3 * i], velocityBytes, true));
		jobs.push_back(new Core::VkBufferJob(_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &_buffers[2 + 3 * i], colorBytes, true));
	}

	Core::CommandBuffer::ImmediateSubmit(_device, jobs);

	for (auto& job : jobs)
	{
		delete(job);
	}
}

void Sample::ParticlePass::Draw(Core::RenderFrame& renderFrame, uint32_t frameIndex, uint32_t imageIndex)
{
	auto& commandBuffer = renderFrame.GetComputeCommandBuffer();

	_deltaTime.deltaTime += 0.01f;
	_computeMaterial->SetBuffer(renderFrame, 0, frameIndex, 0, &_deltaTime.deltaTime);

	_computeMaterial->SetStorageBuffer(renderFrame, 0, 1, _buffers[0]);
	_computeMaterial->SetStorageBuffer(renderFrame, 0, 2, _buffers[1]);
	_computeMaterial->SetStorageBuffer(renderFrame, 0, 3, _buffers[3]);
	_computeMaterial->SetStorageBuffer(renderFrame, 0, 4, _buffers[4]);

	_computeMaterial->SetStorageBuffer(renderFrame, 1, 1, _buffers[3]);
	_computeMaterial->SetStorageBuffer(renderFrame, 1, 2, _buffers[4]);
	_computeMaterial->SetStorageBuffer(renderFrame, 1, 3, _buffers[0]);
	_computeMaterial->SetStorageBuffer(renderFrame, 1, 4, _buffers[1]);

	commandBuffer.BindPipeline(_computePipeline.get());

	commandBuffer.BindDescriptorSets(
		renderFrame,
		_computePipeline->GetPipelineBindPoint(), *_computeMaterial, frameIndex);

	commandBuffer.Dispatch(PARTICLE_COUNT / 256, 1, 1);

	commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());
	auto renderPassBeginInfo =
		_renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	commandBuffer.BindPipeline(_graphicsPipeline.get());

	vector<Core::Buffer*> vertexBuffers(2);
	vertexBuffers[0] = _buffers[frameIndex * 3];
	vertexBuffers[1] = _buffers[frameIndex * 3 + 2];

	commandBuffer.BindVertexBuffers(vertexBuffers, 0);
	commandBuffer.Draw(PARTICLE_COUNT, 1);

	commandBuffer.EndRenderPass();
}
