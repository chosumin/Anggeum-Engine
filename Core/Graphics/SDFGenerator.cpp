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

using namespace Core;

SDFGenerator::SDFGenerator(Device& device)
	: _device(device)
{
	_sdfGenerateShader = _device.GetResourceCache().RequestShader("Shaders/sdfGenerate.comp.spv");
	_sdfGeneratePipeline = make_unique<Pipeline>(_device, *_sdfGenerateShader);
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

void SDFGenerator::Generate(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
	MeshBufferManager& meshBufferManager,
	uint32_t resolution)
{
	if (!_sdfTexture)
	{
		CreateSDFTexture(resolution);
	}

	// Get bounds from MeshBufferManager (accumulated during mesh loading) with padding
	_boundsMin = meshBufferManager.GetSceneBoundsMin();
	_boundsMax = meshBufferManager.GetSceneBoundsMax();
	glm::vec3 padding = (_boundsMax - _boundsMin) * 0.1f;
	_boundsMin -= padding;
	_boundsMax += padding;

	auto& sdfImage = *_sdfTexture->GetImage().lock();
	commandBuffer.TransitionImageLayout(sdfImage,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	uint32_t totalTriangles = meshBufferManager.GetTotalIndexCount() / 3;

	SDFGeneratePushConstants pc{};
	pc.volumeMin = glm::vec4(_boundsMin, 0.0f);
	pc.volumeMax = glm::vec4(_boundsMax, 0.0f);
	pc.resolution = resolution;
	pc.triangleCount = totalTriangles;

	auto sdfBuilder = renderFrame.CreateDescriptorSetBuilder(*_sdfGenerateShader, 0);
	sdfBuilder.SetStorageBuffer(0, meshBufferManager.GetVertexBuffers({"POSITION"})[0]);
	sdfBuilder.SetStorageBuffer(1, &meshBufferManager.GetIndexBuffer());
	sdfBuilder.SetTextureBuffer(2, _sdfTexture, 0, VK_IMAGE_LAYOUT_GENERAL);

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