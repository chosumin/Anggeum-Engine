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
#include "Utils/FileSystem.h"

using namespace Core;

namespace
{
	constexpr uint32_t kSDFFileMagic = 0x56464453u; // 'SDFV'
	constexpr uint32_t kSDFFileVersion = 1u;

	struct SDFFileHeader
	{
		uint32_t magic;
		uint32_t version;
		uint32_t resolution;
		uint32_t format;
		uint32_t rawBounds[8]; // sortable-encoded uint, matches GPU layout
		uint32_t reserved[4];
	};
	static_assert(sizeof(SDFFileHeader) == 64, "SDFFileHeader must be 64 bytes");

	// Job: download SDF image + bounds buffer to host-visible staging buffers.
	class SDFDownloadJob : public Job
	{
	public:
		SDFDownloadJob(Device& device, Image& image, Buffer& boundsBuffer,
			uint32_t resolution, Buffer** outImageStaging, Buffer** outBoundsStaging)
			: Job(JobType::TRANSFER)
			, _device(device), _image(image), _boundsBuffer(boundsBuffer)
			, _resolution(resolution)
			, _outImageStaging(outImageStaging)
			, _outBoundsStaging(outBoundsStaging)
		{
		}

		void Execute() override
		{
			VkDeviceSize voxelBytes = VkDeviceSize(_resolution) * _resolution * _resolution * sizeof(float);

			auto* imageStaging = new Buffer(_device, voxelBytes,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::STAGE);
			auto* boundsStaging = new Buffer(_device, _boundsBuffer.GetSize(),
				VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::STAGE);

			// Image: SHADER_READ_ONLY_OPTIMAL -> TRANSFER_SRC_OPTIMAL
			commandBuffer->TransitionImageLayout(_image,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { _resolution, _resolution, _resolution };

			vkCmdCopyImageToBuffer(commandBuffer->GetHandle(),
				_image.GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				imageStaging->GetBuffer(), 1, &region);

			commandBuffer->TransitionImageLayout(_image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			// Bounds buffer: storage -> staging
			VkBufferCopy bcopy{};
			bcopy.size = _boundsBuffer.GetSize();
			vkCmdCopyBuffer(commandBuffer->GetHandle(),
				_boundsBuffer.GetBuffer(), boundsStaging->GetBuffer(),
				1, &bcopy);

			*_outImageStaging = imageStaging;
			*_outBoundsStaging = boundsStaging;
			status = JobStatus::COMPLETE;
		}

	private:
		Device& _device;
		Image& _image;
		Buffer& _boundsBuffer;
		uint32_t _resolution;
		Buffer** _outImageStaging;
		Buffer** _outBoundsStaging;
	};

	// Job: upload host data to SDF image + bounds buffer.
	class SDFUploadJob : public Job
	{
	public:
		SDFUploadJob(Device& device, Image& image, Buffer& boundsBuffer,
			uint32_t resolution,
			Buffer* imageStaging, Buffer* boundsStaging)
			: Job(JobType::TRANSFER)
			, _device(device), _image(image), _boundsBuffer(boundsBuffer)
			, _resolution(resolution)
			, _imageStaging(imageStaging), _boundsStaging(boundsStaging)
		{
		}

		void Execute() override
		{
			commandBuffer->TransitionImageLayout(_image,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { _resolution, _resolution, _resolution };

			vkCmdCopyBufferToImage(commandBuffer->GetHandle(),
				_imageStaging->GetBuffer(),
				_image.GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &region);

			commandBuffer->TransitionImageLayout(_image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkBufferCopy bcopy{};
			bcopy.size = _boundsStaging->GetSize();
			vkCmdCopyBuffer(commandBuffer->GetHandle(),
				_boundsStaging->GetBuffer(), _boundsBuffer.GetBuffer(),
				1, &bcopy);

			status = JobStatus::COMPLETE;
		}

	private:
		Device& _device;
		Image& _image;
		Buffer& _boundsBuffer;
		uint32_t _resolution;
		Buffer* _imageStaging;
		Buffer* _boundsStaging;
	};
}

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

	_triLookupShader = _device.GetResourceCache().RequestShader("Shaders/sdfTriLookup.comp.spv");
	_triLookupPipeline = make_unique<Pipeline>(_device, *_triLookupShader);

	// Initialize bounds buffer
	uint32_t posInf = FloatToSortableUint(1e20f);
	uint32_t negInf = FloatToSortableUint(-1e20f);
	vector<uint32_t> initData = { posInf, posInf, posInf, 0, negInf, negInf, negInf, 0 };

	VkBufferJob<uint32_t> boundsJob(_device,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
		| VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		&_boundsBuffer, initData, 0);
	CommandBuffer::ImmediateSubmit(_device, boundsJob);
}

SDFGenerator::~SDFGenerator()
{
	if (_boundsBuffer) delete _boundsBuffer;
	if (_triLookupBuffer) delete _triLookupBuffer;
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
	imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

void SDFGenerator::BuildTriangleLookup(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
	Buffer* objectDataBuffer, Buffer* drawCommandBuffer,
	uint32_t drawCommandCount, uint32_t totalTriangles)
{
	// Allocate lookup buffer if needed (2 uints per triangle: vertexOffset + transformIndex)
	VkDeviceSize requiredSize = totalTriangles * sizeof(uint32_t) * 2;

	if (!_triLookupBuffer || _triLookupBuffer->GetSize() < requiredSize)
	{
		if (_triLookupBuffer)
			delete _triLookupBuffer;
		_triLookupBuffer = new Buffer(_device,
			requiredSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			MemoryType::DEVICE_LOCAL);
	}

	auto builder = renderFrame.CreateDescriptorSetBuilder(*_triLookupShader, 0);
	builder.SetStorageBuffer(0, drawCommandBuffer);
	builder.SetStorageBuffer(1, objectDataBuffer);
	builder.SetStorageBuffer(2, _triLookupBuffer);
	auto& resources = builder.Build();

	struct TriLookupPushConstants {
		uint32_t totalTriangles;
		uint32_t drawCommandCount;
	} pc = { totalTriangles, drawCommandCount };

	commandBuffer.BindPipeline(_triLookupPipeline.get());
	commandBuffer.BindDescriptorSet(renderFrame,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		*_triLookupShader, 0, resources);
	commandBuffer.PushConstants(*_triLookupShader, 0, &pc);

	uint32_t groupCount = (totalTriangles + 63) / 64;
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
	Buffer* drawCommandBuffer, uint32_t drawCommandCount,
	uint32_t instanceCount,
	uint32_t resolution)
{
	if (!_sdfTexture)
		CreateSDFTexture(resolution);

	uint32_t totalTriangles = meshBufferManager.GetTotalIndexCount() / 3;

	// Step 1: Compute world-space bounds
	ComputeWorldBounds(renderFrame, commandBuffer,
		objectDataBuffer, transformBuffer, instanceCount);

	// Step 2: Build per-triangle lookup (vertexOffset + transformIndex)
	BuildTriangleLookup(renderFrame, commandBuffer,
		objectDataBuffer, drawCommandBuffer,
		drawCommandCount, totalTriangles);

	// Step 3: Generate SDF volume
	auto& sdfImage = *_sdfTexture->GetImage().lock();
	commandBuffer.TransitionImageLayout(sdfImage,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	SDFGeneratePushConstants pc{};
	pc.resolution = resolution;
	pc.triangleCount = totalTriangles;
	pc.paddingFactor = _paddingFactor;
	pc.useUint16Indices = (meshBufferManager.GetIndexType() == VK_INDEX_TYPE_UINT16) ? 1 : 0;

	auto sdfBuilder = renderFrame.CreateDescriptorSetBuilder(*_sdfGenerateShader, 0);
	sdfBuilder.SetStorageBuffer(0, meshBufferManager.GetVertexBuffers({ "POSITION" })[0]);
	sdfBuilder.SetStorageBuffer(1, &meshBufferManager.GetIndexBuffer());
	sdfBuilder.SetTextureBuffer(2, _sdfTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
	sdfBuilder.SetStorageBuffer(3, _boundsBuffer);
	sdfBuilder.SetStorageBuffer(4, _triLookupBuffer);
	sdfBuilder.SetStorageBuffer(5, transformBuffer);

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

bool SDFGenerator::SaveToFile(uint32_t resolution)
{
	if (!_sdfTexture || !_boundsBuffer)
		return false;

	auto image = _sdfTexture->GetImage().lock();
	if (!image)
		return false;

	// The SDF generation commands were recorded into a frame command buffer that
	// may still be executing (or not yet started) on the GPU. The download job
	// below reads the SDF image, so all prior GPU work must complete first.
	// SaveToFile is an infrequent (button / first-gen) operation, so a full
	// device wait is acceptable here.
	vkDeviceWaitIdle(_device.GetDevice());

	Buffer* imageStaging = nullptr;
	Buffer* boundsStaging = nullptr;

	SDFDownloadJob job(_device, *image, *_boundsBuffer, resolution,
		&imageStaging, &boundsStaging);
	CommandBuffer::ImmediateSubmit(_device, job);

	if (!imageStaging || !boundsStaging)
		return false;

	// Build header (bounds stored so loaded volume's coordinate system matches)
	SDFFileHeader header{};
	header.magic = kSDFFileMagic;
	header.version = kSDFFileVersion;
	header.resolution = resolution;
	header.format = VK_FORMAT_R32_SFLOAT;

	void* boundsMapped = nullptr;
	boundsStaging->Map(&boundsMapped);
	memcpy(header.rawBounds, boundsMapped, sizeof(header.rawBounds));
	boundsStaging->Unmap();

	VkDeviceSize voxelBytes = VkDeviceSize(resolution) * resolution * resolution * sizeof(float);

	// Concatenate header + voxel data into one contiguous blob for a single write
	vector<uint8_t> fileData(sizeof(SDFFileHeader) + static_cast<size_t>(voxelBytes));
	memcpy(fileData.data(), &header, sizeof(SDFFileHeader));

	void* imageMapped = nullptr;
	imageStaging->Map(&imageMapped);
	memcpy(fileData.data() + sizeof(SDFFileHeader), imageMapped, static_cast<size_t>(voxelBytes));
	imageStaging->Unmap();

	delete imageStaging;
	delete boundsStaging;

	try
	{
		FileSystem::Write(_sdfCachePath, fileData.data(), fileData.size());
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool SDFGenerator::TryLoadFromFile(uint32_t expectedResolution)
{
	if (!FileSystem::Exists(_sdfCachePath))
		return false;

	vector<uint8_t> fileData;
	try
	{
		fileData = FileSystem::Read(_sdfCachePath); // count = 0 reads whole file
	}
	catch (const std::exception&)
	{
		return false;
	}

	if (fileData.size() < sizeof(SDFFileHeader))
		return false;

	SDFFileHeader header{};
	memcpy(&header, fileData.data(), sizeof(SDFFileHeader));

	if (header.magic != kSDFFileMagic || header.version != kSDFFileVersion)
		return false;
	if (header.resolution != expectedResolution)
		return false;
	if (header.format != VK_FORMAT_R32_SFLOAT)
		return false;

	VkDeviceSize voxelBytes = VkDeviceSize(header.resolution) * header.resolution
		* header.resolution * sizeof(float);

	if (fileData.size() < sizeof(SDFFileHeader) + voxelBytes)
		return false;

	// Voxel staging
	auto* imageStaging = new Buffer(_device, voxelBytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::STAGE);
	void* imageMapped = nullptr;
	imageStaging->Map(&imageMapped);
	memcpy(imageMapped, fileData.data() + sizeof(SDFFileHeader), static_cast<size_t>(voxelBytes));
	imageStaging->Unmap();

	// Bounds staging
	auto* boundsStaging = new Buffer(_device, _boundsBuffer->GetSize(),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::STAGE);
	void* boundsMapped = nullptr;
	boundsStaging->Map(&boundsMapped);
	memcpy(boundsMapped, header.rawBounds, sizeof(header.rawBounds));
	boundsStaging->Unmap();

	if (!_sdfTexture)
		CreateSDFTexture(header.resolution);

	auto image = _sdfTexture->GetImage().lock();
	if (!image)
	{
		delete imageStaging;
		delete boundsStaging;
		return false;
	}

	SDFUploadJob job(_device, *image, *_boundsBuffer, header.resolution,
		imageStaging, boundsStaging);
	CommandBuffer::ImmediateSubmit(_device, job);

	delete imageStaging;
	delete boundsStaging;

	_generated = true;
	return true;
}