#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "Foundation/Scene.h"
#include "Foundation/WorkerThread.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/RenderContext.h"
#include "Graphics/ResourceCache.h"
#include "Graphics/TransferJob.h"
#include "Graphics/RendererPasses/DepthPrePass.h"
#include "Graphics/RendererPasses/LightCullingPass.h"
#include "Sample/RendererPasses/GeometryPass.h"
#include "Sample/RendererPasses/ShadowPass.h"
#include "Utils/Utility.h"
using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(Device& device, 
	WorkerThreadManager& workerThreadManager,
	Scene& scene, SwapChain& swapChain)
	:_device(device)
{
	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::AddResizeCallback(a);

	_msaaSamples = GetMaxUsableSampleCount();

	_sampler = device.GetResourceCache().RequestSampler(DEFAULT_SAMPLER);

	auto extent = swapChain.GetSwapChainExtent();

	_renderTargets.push_back(CreateColorRenderTarget(extent, swapChain.GetImageFormat(), false));
	_renderTargets.push_back(CreateDepthRenderTarget(extent, true, _msaaSamples));
	_renderTargets.push_back(CreateDepthRenderTarget(extent, true, VK_SAMPLE_COUNT_1_BIT));

	CreatePreSkyTextures();

	ivec2 tileNums = ivec2(
		(extent.width - 1) / TILE_SIZE + 1,
		(extent.height - 1) / TILE_SIZE + 1);
	CreateLightCullingBuffer(extent, tileNums);

	CreateTransformBuffer(scene);

	auto depthPrePass = new DepthPrePass(device, workerThreadManager, scene, swapChain, _renderTargets[1], _transformBatch);
	AddRendererPass(depthPrePass);

	auto shadowPass = new ShadowPass(
		device, workerThreadManager, scene, swapChain, _renderTargets[2], _transformBatch);
	AddRendererPass(shadowPass);

	auto lightCullingPass = new LightCullingPass(device, workerThreadManager, scene, swapChain.GetSwapChainExtent(), tileNums, 
		_renderTargets[1], _lightBuffer);
	AddRendererPass(lightCullingPass);

	auto geometryPass = new GeometryPass(
		device, workerThreadManager, scene, swapChain,
		_renderTargets[0], _renderTargets[1], 
		_giBuffer,
		_renderTargets[2], _renderTargets[3], 
		_renderTargets[4], _renderTargets[5],
		_renderTargets[6],
		_lightBuffer, tileNums,
		_transformBatch);

	geometryPass->SetBuffer(shadowPass->GetShadowBuffer());
	AddRendererPass(geometryPass);
}

Core::ForwardRenderPipeline::~ForwardRenderPipeline()
{
	Cleanup();

	for (auto&& rendererPass : _rendererPasses)
	{
		delete(rendererPass);
	}

	delete(_lightBuffer);
	delete(_transformBatch.TransformBuffer);

	auto a = std::bind(&ForwardRenderPipeline::Resize, this, std::placeholders::_1);
	Core::RenderContext::RemoveResizeCallback(a);
}

void ForwardRenderPipeline::Prepare()
{
	for (auto&& rendererPass : _rendererPasses)
	{
		rendererPass->Prepare();
	}
}

void ForwardRenderPipeline::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	for (auto&& rendererPass : _rendererPasses)
	{
		rendererPass->Draw(renderFrame, imageIndex);
	}
}

void Core::ForwardRenderPipeline::Cleanup()
{
	_renderTargets.clear();
}

void Core::ForwardRenderPipeline::Resize(SwapChain& swapChain)
{
	Cleanup();

	VkExtent2D extent = swapChain.GetSwapChainExtent();

	/*if (_color != nullptr)
	{
		CreateColorRenderTarget(extent, _color->Format, _color->LoadOp, _color->StoreOp, _color->InitialLayout);
	}

	if (_depth != nullptr)
	{
		CreateDepthRenderTarget(extent, _depth->LoadOp, _depth->StoreOp);
	}

	for (auto& renderTarget : _inputRenderTargets)
	{
		CreateRenderTarget(extent, renderTarget->Format, renderTarget->Layout, renderTarget->UsageFlags, renderTarget->LoadOp, renderTarget->StoreOp);
	}*/
}

VkSampleCountFlagBits Core::ForwardRenderPipeline::GetMaxUsableSampleCount()
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(
		_device.GetPhysicalDevice(),
		&physicalDeviceProperties);

	VkSampleCountFlags counts =
		physicalDeviceProperties.limits.framebufferColorSampleCounts &
		physicalDeviceProperties.limits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

shared_ptr<Texture> Core::ForwardRenderPipeline::CreateRenderTarget(VkExtent2D extent, VkFormat format, VkImageLayout layout, VkImageUsageFlags usageFlags)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { extent.width, extent.height, 1 };
	imageInfo.format = format;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = _msaaSamples;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = usageFlags;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	
	shared_ptr<Image> image = make_shared<Image>(_device, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);
	shared_ptr<Texture> renderTarget = make_shared<Texture>("render target", image, _sampler);
	return renderTarget;
}

shared_ptr<Texture> Core::ForwardRenderPipeline::CreateDepthRenderTarget(VkExtent2D extent, bool isUsedAsSource, VkSampleCountFlagBits sampleCount, bool isStorageImage)
{
	auto depthFormat = _device.FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	VkImageUsageFlags flags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (isUsedAsSource)
		flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (isStorageImage)
		flags |= VK_IMAGE_USAGE_STORAGE_BIT;

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { extent.width, extent.height, 1 };
	imageInfo.format = depthFormat;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = sampleCount;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = flags;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto image = make_shared<Image>(_device, imageInfo, VK_IMAGE_ASPECT_DEPTH_BIT);

	shared_ptr<Texture> renderTarget = make_shared<Texture>("depth target", image, _sampler);
	return renderTarget;
}

shared_ptr<Texture> Core::ForwardRenderPipeline::CreateColorRenderTarget(VkExtent2D extent, VkFormat format, bool isUsedAsSource, bool isStorageImage)
{
	VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (isUsedAsSource)
		flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (isStorageImage)
		flags |= VK_IMAGE_USAGE_STORAGE_BIT;

	//VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : gpu virtual address and not physical memory pages.
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { extent.width, extent.height, 1 };
	imageInfo.format = format;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = _msaaSamples;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = flags;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto image = make_shared<Image>(_device, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);

	shared_ptr<Texture> renderTarget = make_shared<Texture>("color target", image, _sampler);

	return renderTarget;
}

void Core::ForwardRenderPipeline::CreatePreSkyTextures()
{
	uint32_t size = 128;
	const uint32_t numMips = static_cast<uint32_t>(floor(std::log2(size))) + 1;
	VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
	
	{
		//Offscreen texture to blit to the cubemap
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { size , size , 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		auto image = make_shared<Image>(_device, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);
		shared_ptr<Texture> offscreen = make_shared<Texture>("offscreen", image, _sampler);
		_renderTargets.push_back(offscreen);
	}

	{
		//Irradiance cubemap
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { size , size , 1 };
		imageInfo.mipLevels = numMips;
		imageInfo.arrayLayers = 6;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		auto image = make_shared<Image>(_device, imageInfo,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_VIEW_TYPE_CUBE);

		shared_ptr<Texture> cubemap = make_shared<Texture>("irradiance", image, _sampler);
		_renderTargets.push_back(cubemap);
	}

	{
		//Prefiltered cubemap
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { size , size , 1 };
		imageInfo.mipLevels = numMips;
		imageInfo.arrayLayers = 6;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		auto image = make_shared<Image>(_device, imageInfo,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_VIEW_TYPE_CUBE);

		shared_ptr<Texture> cubemap = make_shared<Texture>("prefiltered", image, _sampler);
		_renderTargets.push_back(cubemap);
	}

	{
		//BRDF LUT
		VkFormat format = VK_FORMAT_R16G16_SFLOAT;
		uint32_t size = 512;

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { size , size , 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		auto image = make_shared<Image>(_device, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);

		shared_ptr<Texture> bdrf = make_shared<Texture>("brdflut", image, _sampler);
		_renderTargets.push_back(bdrf);
	}
}

void Core::ForwardRenderPipeline::CreateLightCullingBuffer(VkExtent2D extent, ivec2 tileNums)
{
	u32 lightVisiblityBufferSize = sizeof(VisibleLightsForTile) * tileNums.x * tileNums.y;

	auto lightVisibilityBuffer = new Core::Buffer(_device,
		lightVisiblityBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	_lightBuffer = lightVisibilityBuffer;
}

void Core::ForwardRenderPipeline::CreateTransformBuffer(Scene& scene)
{
	auto meshes = scene.GetComponents<Core::Mesh>();
	
	uint meshCount = meshes.size();
	uint bufferSize = sizeof(mat4) * meshCount;

	vector<mat4> transforms(meshCount);
	_transformBatch.EntityIds.resize(meshCount);

	for (uint i = 0; i < meshCount; ++i)
	{
		auto& entity = meshes[i]->GetEntity();
		auto& transform = entity.GetTransform();
		transforms[i] = transform.GetMatrix();
		_transformBatch.EntityIds[i] = entity.GetId();
	}

	_transformBatch.TransformBuffer = new Core::Buffer(_device,
		bufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	Core::VkBufferJob<mat4> job(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &_transformBatch.TransformBuffer, transforms, true);
	Core::CommandBuffer::ImmediateSubmit(_device, job);
}

void Core::ForwardRenderPipeline::RegisterGiTexturesToBindless(RenderContext& renderContext)
{
	if (!renderContext.HasBindlessSupport())
		return;

	auto* bindlessManager = renderContext.GetBindlessTextureManager();

	TextureHandle shadowmapHandle = bindlessManager->RegisterTexture(_renderTargets[2]);
	TextureHandle irradianceCubemapHandle = bindlessManager->RegisterTexture(_renderTargets[4]);
	TextureHandle prefilteredCubemapHandle = bindlessManager->RegisterTexture(_renderTargets[5]);
	TextureHandle brdfLutHandle = bindlessManager->RegisterTexture(_renderTargets[6]);

	_giBuffer.irradianceMapIndex = irradianceCubemapHandle.index;
	_giBuffer.prefilterMapIndex = prefilteredCubemapHandle.index;
	_giBuffer.brdfLUTIndex = brdfLutHandle.index;
	_giBuffer.shadowmapIndex = shadowmapHandle.index;

	cout << "GI textures registered to bindless:" << endl;
	cout << "  Irradiance cubemap: index " << irradianceCubemapHandle.index << endl;
	cout << "  Prefiltered cubemap: index " << prefilteredCubemapHandle.index << endl;
	cout << "  BRDF LUT: index " << brdfLutHandle.index << endl;
	cout << "  Shadowmap: index " << shadowmapHandle.index << endl;
}