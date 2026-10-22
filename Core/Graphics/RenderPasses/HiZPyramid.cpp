#include "stdafx.h"
#include "HiZPyramid.h"
#include "Graphics/FrameResources.h"
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Texture.h"

using namespace Core;

uint32_t HiZPyramid::MipLevelsFor(VkExtent2D extent)
{
	uint32_t maxDim = std::max(extent.width, extent.height);
	return static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;
}

HiZPyramid::HiZPyramid(ResourceManager& resourceManager, VkExtent2D extent)
	: _resourceManager(resourceManager)
	, _extent(extent)
	, _mipLevels(MipLevelsFor(extent))
{

	_shader = resourceManager.LoadShader("Shaders/hiZGenerate.comp.spv");
	_pipeline = resourceManager.LoadComputePipeline("Shaders/hiZGenerate.comp.spv");

	// Occlusion sampling must be point-filtered.
	auto samplerDesc = DEFAULT_SAMPLER;
	samplerDesc.minFilter = VK_FILTER_NEAREST;
	samplerDesc.magFilter = VK_FILTER_NEAREST;
	samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	_sampler = resourceManager.LoadSampler(samplerDesc);
}

HiZPyramid::~HiZPyramid() = default;

Handle<Texture> HiZPyramid::EnsureTexture(FrameResources& frameResources, const char* name)
{
	RenderTargetDesc desc{};
	desc.extent = _extent;
	desc.format = VK_FORMAT_R32_SFLOAT;
	desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	desc.mipLevels = _mipLevels;
	desc.sampler = _sampler;
	// A cull may bind the image before any build has filled it.
	desc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	Handle<Texture> texture = frameResources.GetOrCreateRenderTarget(name, desc);

	Image& image = texture.Get().GetImage();
	for (uint32_t mip = 1; mip < _mipLevels; ++mip)
		image.GetOrCreateImageView(mip);

	return texture;
}

void HiZPyramid::Build(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
	Texture& depth, Texture& hiZ)
{
	commandBuffer.CreateBarrierBatch()
		.Image(hiZ, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		.Submit();

	commandBuffer.CopyImage(depth, hiZ, 0, 0, 0, 0);

	commandBuffer.CreateBarrierBatch()
		.Image(hiZ, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL)
		.Submit();

	if (_mipLevels > 1)
	{
		commandBuffer.BindPipeline(&_pipeline.Get());
		Shader& shader = _shader.Get();

		uint32_t mipWidth = _extent.width;
		uint32_t mipHeight = _extent.height;

		for (uint32_t mip = 1; mip < _mipLevels; ++mip)
		{
			mipWidth = std::max(1u, mipWidth / 2);
			mipHeight = std::max(1u, mipHeight / 2);

			auto builder = context.CreateDescriptorSetBuilder(shader, 0);
			builder.SetTextureBuffer(0, hiZ, mip - 1, VK_IMAGE_LAYOUT_GENERAL);
			builder.SetTextureBuffer(1, hiZ, mip, VK_IMAGE_LAYOUT_GENERAL);
			auto& resources = builder.Build();

			commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, shader, resources);

			struct HiZPushConstants
			{
				int32_t outputWidth;
				int32_t outputHeight;
			} pc = { static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) };
			commandBuffer.PushConstants(shader, 0, pc);

			commandBuffer.Dispatch((mipWidth + 7) / 8, (mipHeight + 7) / 8, 1);

			commandBuffer.CreateBarrierBatch()
				.Image(hiZ, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL)
				.Submit();
		}
	}

	commandBuffer.CreateBarrierBatch()
		.Image(hiZ, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		.Submit();
}
