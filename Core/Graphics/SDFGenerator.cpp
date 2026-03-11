#include "stdafx.h"
#include "SDFGenerator.h"
#include "Vulkans/Device.h"
#include "Vulkans/Image.h"
#include "Vulkans/Texture.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/Shader.h"
#include "Vulkans/Pipeline.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "MeshBufferManager.h"
#include "RenderFrame.h"
#include "ResourceCache.h"
#include "TransferJob.h"

using namespace Core;

static uint32_t FloatToSortableUint(float f)
{
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	uint32_t mask = (bits & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
	return bits ^ mask;
}

SDFGenerator::SDFGenerator(Device& device)
	: _device(device)
{
	_sdfGenerateShader = _device.GetResourceCache().RequestShader("Shaders/sdfGenerate.comp.spv");
	_sdfGeneratePipeline = make_unique<Pipeline>(_device, *_sdfGenerateShader);

	_boundsReduceShader = _device.GetResourceCache().RequestShader("Shaders/sdfBoundsReduce.comp.spv");
	_boundsReducePipeline = make_unique<Pipeline>(_device, *_boundsReduceShader);

	// Initialize bounds buffer with identity values via VkBufferJob
	uint32_t posInf = FloatToSortableUint(1e20f);
	uint32_t negInf = FloatToSortableUint(-1e20f);
	vector<uint32_t> initData = { posInf, posInf, posInf, 0, negInf, negInf, negInf, 0 };

	VkBufferJob<uint32_t> boundsJob(_device,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		&_boundsBuffer, initData, 0);
	CommandBuffer::ImmediateSubmit(_device, boundsJob);
}

SDFGenerator::~SDFGenerator()
{
	if (_boundsBuffer) delete _boundsBuffer;
}

void SDFGenerator::CreateSDFTexture(uint32_t resolution)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_3D;
	imageInfo.extent = { resolution, resolution, resolution };
	imageInfo.format = VK_FORMAT_R32_SFLOAT;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto image = make_shared<Image>(_device, imageInfo,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_3D);

	auto sampler = _device.GetResourceCache().RequestSampler(DEFAULT_SAMPLER);
	_sdfTexture = make_shared<Texture>("SDFVolume", image, sampler);
}

void SDFGenerator::ComputeWorldBounds(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
	Buffer* objectDataBuffer, Buffer* transformBuffer,
	uint32_t instanceCount)
{
	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	auto builder = renderFrame.CreateDescriptorSetBuilder(*_boundsReduceShader, 0);
	builder.SetStorageBuffer(0, objectDataBuffer);
	builder.SetStorageBuffer(1, transformBuffer);
	builder.SetStorageBuffer(2, _boundsBuffer);
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(_boundsReducePipeline.get());
	commandBuffer.BindDescriptorSet(renderFrame,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		*_boundsReduceShader, 0, resources);
	commandBuffer.PushConstants(*_boundsReduceShader, 0, &instanceCount);

	uint32_t groupCount = (instanceCount + 63) / 64;
	commandBuffer.Dispatch(groupCount, 1, 1);

	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT);
}

void SDFGenerator::Generate(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
	MeshBufferManager& meshBufferManager,
	Buffer* objectDataBuffer, Buffer* transformBuffer,
	uint32_t instanceCount,
	uint32_t resolution)
{
	if (!_sdfTexture)
	{
		CreateSDFTexture(resolution);
	}

	// Compute world-space bounds entirely on GPU (single pass)
	ComputeWorldBounds(renderFrame, commandBuffer,
		objectDataBuffer, transformBuffer, instanceCount);

	// Generate SDF volume ? reads encoded bounds via SSBO, decodes in shader
	auto& sdfImage = *_sdfTexture->GetImage().lock();
	commandBuffer.TransitionImageLayout(sdfImage,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	uint32_t totalTriangles = meshBufferManager.GetTotalIndexCount() / 3;

	SDFGeneratePushConstants pc{};
	pc.resolution = resolution;
	pc.triangleCount = totalTriangles;
	pc.paddingFactor = _paddingFactor;

	auto sdfBuilder = renderFrame.CreateDescriptorSetBuilder(*_sdfGenerateShader, 0);
	sdfBuilder.SetStorageBuffer(0, meshBufferManager.GetVertexBuffers({"POSITION"})[0]);
	sdfBuilder.SetStorageBuffer(1, &meshBufferManager.GetIndexBuffer());
	sdfBuilder.SetTextureBuffer(2, _sdfTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
	sdfBuilder.SetStorageBuffer(3, _boundsBuffer);

	auto& sdfResources = sdfBuilder.Build();
	commandBuffer.BindPipeline(_sdfGeneratePipeline.get());
	commandBuffer.BindDescriptorSet(renderFrame,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		*_sdfGenerateShader, 0, sdfResources);
	commandBuffer.PushConstants(*_sdfGenerateShader, 0, &pc);

	uint32_t groups = (resolution + 3) / 4;
	commandBuffer.Dispatch(groups, groups, groups);

	commandBuffer.TransitionImageLayout(sdfImage,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	_generated = true;
}