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
#include "FrameResources.h"
#include "RenderFrame.h"
#include "ResourceManager.h"
#include "BufferUpload.h"
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
		SDFDownloadJob(Device& device, Texture& texture, Buffer& boundsBuffer,
			uint32_t resolution, Buffer** outImageStaging, Buffer** outBoundsStaging)
			: Job(JobType::TRANSFER)
			, _device(device), _texture(texture), _boundsBuffer(boundsBuffer)
			, _resolution(resolution)
			, _outImageStaging(outImageStaging)
			, _outBoundsStaging(outBoundsStaging)
		{
		}

		void Execute() override
		{
			VkDeviceSize voxelBytes = VkDeviceSize(_resolution) * _resolution * _resolution * sizeof(float);

			auto* imageStaging = new Buffer(_device, voxelBytes,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::DEDICATED_HOST);
			auto* boundsStaging = new Buffer(_device, _boundsBuffer.GetSize(),
				VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::DEDICATED_HOST);

			// Image: SHADER_READ_ONLY_OPTIMAL -> TRANSFER_SRC_OPTIMAL
			commandBuffer->CreateBarrierBatch()
				.Image(_texture,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
				.Submit();

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { _resolution, _resolution, _resolution };

			vkCmdCopyImageToBuffer(commandBuffer->GetHandle(),
				_texture.GetImage().GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				imageStaging->GetBuffer(), 1, &region);

			commandBuffer->CreateBarrierBatch()
				.Image(_texture,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.Submit();

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
		Texture& _texture;
		Buffer& _boundsBuffer;
		uint32_t _resolution;
		Buffer** _outImageStaging;
		Buffer** _outBoundsStaging;
	};

	// Job: upload host data to SDF image + bounds buffer.
	class SDFUploadJob : public Job
	{
	public:
		SDFUploadJob(Device& device, Texture& texture, Buffer& boundsBuffer,
			uint32_t resolution,
			Buffer* imageStaging, Buffer* boundsStaging)
			: Job(JobType::TRANSFER)
			, _device(device), _texture(texture), _boundsBuffer(boundsBuffer)
			, _resolution(resolution)
			, _imageStaging(imageStaging), _boundsStaging(boundsStaging)
		{
		}

		void Execute() override
		{
			commandBuffer->CreateBarrierBatch()
				.Image(_texture,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
				.Submit();

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { _resolution, _resolution, _resolution };

			vkCmdCopyBufferToImage(commandBuffer->GetHandle(),
				_imageStaging->GetBuffer(),
				_texture.GetImage().GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &region);

			commandBuffer->CreateBarrierBatch()
				.Image(_texture,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.Submit();

			VkBufferCopy bcopy{};
			bcopy.size = _boundsStaging->GetSize();
			vkCmdCopyBuffer(commandBuffer->GetHandle(),
				_boundsStaging->GetBuffer(), _boundsBuffer.GetBuffer(),
				1, &bcopy);

			status = JobStatus::COMPLETE;
		}

	private:
		Device& _device;
		Texture& _texture;
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

SDFGenerator::SDFGenerator(Device& device, ResourceManager& resourceManager)
	: _device(device)
	, _resourceManager(resourceManager)
{
	_sdfGenerateShader = _resourceManager.LoadShader("Shaders/sdfGenerate.comp.spv");
	_sdfGeneratePipeline = _resourceManager.LoadComputePipeline("Shaders/sdfGenerate.comp.spv");

	_boundsReduceShader = _resourceManager.LoadShader("Shaders/sdfBoundsReduce.comp.spv");
	_boundsReducePipeline = _resourceManager.LoadComputePipeline("Shaders/sdfBoundsReduce.comp.spv");

	_triLookupShader = _resourceManager.LoadShader("Shaders/sdfTriLookup.comp.spv");
	_triLookupPipeline = _resourceManager.LoadComputePipeline("Shaders/sdfTriLookup.comp.spv");

	// Initialize bounds buffer (pool-owned; allocate then fill via a copy job).
	uint32_t posInf = FloatToSortableUint(1e20f);
	uint32_t negInf = FloatToSortableUint(-1e20f);
	vector<uint32_t> initData = { posInf, posInf, posInf, 0, negInf, negInf, negInf, 0 };

	_boundsBuffer = _resourceManager.LoadBuffer(
		{ initData.size() * sizeof(uint32_t),
		  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
		  | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		  MemoryType::DEVICE_LOCAL },
		"SDF.Bounds");

	BufferUploadJob<uint32_t> boundsJob(_device, _boundsBuffer.Get(), move(initData), 0);
	CommandBuffer::ImmediateSubmit(_device, boundsJob);
}

SDFGenerator::~SDFGenerator() = default;

void SDFGenerator::CreateSDFTexture(uint32_t resolution)
{
	// depth > 1 makes the unified path create a 3D image (and a 3D view).
	ImageDesc imageDesc{};
	imageDesc.extent = { resolution, resolution };
	imageDesc.depth = resolution;
	imageDesc.format = VK_FORMAT_R32_SFLOAT;
	imageDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	auto image = make_unique<Image>(_device, imageDesc);

	auto sampler = _resourceManager.LoadSampler(DEFAULT_SAMPLER);
	_sdfTexture = _resourceManager.LoadTexture("SDFVolume", std::move(image), sampler);
}

void SDFGenerator::ComputeWorldBounds(FrameResources& frameResources, CommandBuffer& commandBuffer,
	Buffer& objectDataBuffer, Buffer& transformBuffer,
	uint32_t instanceCount)
{
	// Make the transfer-uploaded inputs (and the initialized bounds buffer) visible
	// to the reduce kernel. Scoped to the buffers it reads rather than all memory.
	commandBuffer.CreateBarrierBatch()
		.Buffer(objectDataBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT)
		.Buffer(transformBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT)
		.Buffer(_boundsBuffer.Get(),
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Submit();

	auto& boundsReduceShader = _boundsReduceShader.Get();
	auto builder = frameResources.CreateDescriptorSetBuilder(boundsReduceShader, 0);
	builder.SetStorageBuffer(0, objectDataBuffer);
	builder.SetStorageBuffer(1, transformBuffer);
	builder.SetStorageBuffer(2, _boundsBuffer.Get());
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(&_boundsReducePipeline.Get());
	commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
		boundsReduceShader,
		resources);
	commandBuffer.PushConstants(boundsReduceShader, 0, instanceCount);

	uint32_t groupCount = (instanceCount + 63) / 64;
	commandBuffer.Dispatch(groupCount, 1, 1);

	// The reduce kernel writes the bounds buffer; the SDF generate pass reads it.
	commandBuffer.CreateBarrierBatch()
		.Buffer(_boundsBuffer.Get(),
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT)
		.Submit();
}

void SDFGenerator::BuildTriangleLookup(FrameResources& frameResources, CommandBuffer& commandBuffer,
	Buffer& objectDataBuffer, Buffer& drawCommandBuffer,
	uint32_t drawCommandCount, uint32_t totalTriangles)
{
	// Allocate lookup buffer if needed (2 uints per triangle: vertexOffset + transformIndex).
	// It grows with triangle count: first allocation adds a slot, a later grow swaps
	// the backing buffer in place (Replace) so the handle stays valid.
	VkDeviceSize requiredSize = totalTriangles * sizeof(uint32_t) * 2;

	auto& resourceManager = _resourceManager;
	BufferDesc triLookupDesc{ requiredSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::DEVICE_LOCAL };
	if (!_triLookupBuffer.IsValid())
	{
		_triLookupBuffer = resourceManager.LoadBuffer(triLookupDesc, "SDF.TriLookup");
	}
	else if (_triLookupBuffer.Get().GetSize() < requiredSize)
	{
		resourceManager.ResizeBuffer(_triLookupBuffer, triLookupDesc, "SDF.TriLookup");
	}

	auto& triLookupShader = _triLookupShader.Get();
	auto builder = frameResources.CreateDescriptorSetBuilder(triLookupShader, 0);
	builder.SetStorageBuffer(0, drawCommandBuffer);
	builder.SetStorageBuffer(1, objectDataBuffer);
	builder.SetStorageBuffer(2, _triLookupBuffer.Get());
	auto& resources = builder.Build();

	struct TriLookupPushConstants {
		uint32_t totalTriangles;
		uint32_t drawCommandCount;
	} pc = { totalTriangles, drawCommandCount };

	commandBuffer.BindPipeline(&_triLookupPipeline.Get());
	commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
		triLookupShader,
		resources);
	commandBuffer.PushConstants(triLookupShader, 0, pc);

	uint32_t groupCount = (totalTriangles + 63) / 64;
	commandBuffer.Dispatch(groupCount, 1, 1);

	// The lookup kernel writes the triangle-lookup buffer; the SDF generate pass reads it.
	commandBuffer.CreateBarrierBatch()
		.Buffer(_triLookupBuffer.Get(),
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT)
		.Submit();
}

void SDFGenerator::Generate(FrameResources& frameResources, RenderFrame& renderFrame,
	CommandBuffer& commandBuffer,
	uint32_t resolution)
{
	if (!_sdfTexture.IsValid())
		CreateSDFTexture(resolution);

	auto& meshBufferManager = renderFrame.GetMeshBufferManager();
	auto& batch = renderFrame.GetRendererBatch();
	auto& objectDataBuffer = batch.GetObjectDataBuffer();
	auto& indirectCommandBuffer = batch.GetIndirectCommandBuffer();
	auto drawCommandCount = batch.GetDrawCommandCount();
	auto instanceCount = batch.GetInstanceCount();
	auto& transformBuffer = batch.GetTransformBatch().TransformBuffer.Get();

	uint32_t totalTriangles = meshBufferManager.GetTotalIndexCount() / 3;

	// Step 1: Compute world-space bounds
	ComputeWorldBounds(frameResources, commandBuffer,
		objectDataBuffer, transformBuffer, instanceCount);

	// Step 2: Build per-triangle lookup (vertexOffset + transformIndex)
	BuildTriangleLookup(frameResources, commandBuffer,
		objectDataBuffer, indirectCommandBuffer,
		drawCommandCount, totalTriangles);

	// Step 3: Generate SDF volume
	commandBuffer.CreateBarrierBatch()
		.Image(_sdfTexture.Get(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL)
		.Submit();

	SDFGeneratePushConstants pc{};
	pc.resolution = resolution;
	pc.triangleCount = totalTriangles;
	pc.paddingFactor = _paddingFactor;
	pc.useUint16Indices = (meshBufferManager.GetIndexType() == VK_INDEX_TYPE_UINT16) ? 1 : 0;

	auto& sdfGenerateShader = _sdfGenerateShader.Get();
	auto sdfBuilder = frameResources.CreateDescriptorSetBuilder(sdfGenerateShader, 0);
	sdfBuilder.SetStorageBuffer(0, meshBufferManager.GetVertexBuffers({ "POSITION" })[0].Get());
	sdfBuilder.SetStorageBuffer(1, meshBufferManager.GetIndexBuffer().Get());
	sdfBuilder.SetTextureBuffer(2, _sdfTexture.Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	sdfBuilder.SetStorageBuffer(3, _boundsBuffer.Get());
	sdfBuilder.SetStorageBuffer(4, _triLookupBuffer.Get());
	sdfBuilder.SetStorageBuffer(5, transformBuffer);

	auto& sdfResources = sdfBuilder.Build();
	commandBuffer.BindPipeline(&_sdfGeneratePipeline.Get());
	commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
		sdfGenerateShader,
		sdfResources);
	commandBuffer.PushConstants(sdfGenerateShader, 0, pc);

	uint32_t groups = (resolution + 3) / 4;
	commandBuffer.Dispatch(groups, groups, groups);

	commandBuffer.CreateBarrierBatch()
		.Image(_sdfTexture.Get(),
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		.Submit();

	_generated = true;
}

bool SDFGenerator::SaveToFile(uint32_t resolution)
{
	if (!_sdfTexture.IsValid() || !_boundsBuffer.IsValid())
		return false;

	auto& texture = _sdfTexture.Get();

	// The SDF generation commands were recorded into a frame command buffer that
	// may still be executing (or not yet started) on the GPU. The download job
	// below reads the SDF image, so all prior GPU work must complete first.
	// SaveToFile is an infrequent (button / first-gen) operation, so a full
	// device wait is acceptable here.
	vkDeviceWaitIdle(_device.GetDevice());

	Buffer* imageStaging = nullptr;
	Buffer* boundsStaging = nullptr;

	SDFDownloadJob job(_device, texture, _boundsBuffer.Get(), resolution,
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
	boundsStaging->GetMappedPtr(&boundsMapped);
	memcpy(header.rawBounds, boundsMapped, sizeof(header.rawBounds));

	VkDeviceSize voxelBytes = VkDeviceSize(resolution) * resolution * resolution * sizeof(float);

	// Concatenate header + voxel data into one contiguous blob for a single write
	vector<uint8_t> fileData(sizeof(SDFFileHeader) + static_cast<size_t>(voxelBytes));
	memcpy(fileData.data(), &header, sizeof(SDFFileHeader));

	void* imageMapped = nullptr;
	imageStaging->GetMappedPtr(&imageMapped);
	memcpy(fileData.data() + sizeof(SDFFileHeader), imageMapped, static_cast<size_t>(voxelBytes));

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
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::DEDICATED_HOST);
	imageStaging->CopyBuffer(fileData.data() + sizeof(SDFFileHeader), voxelBytes);

	// Bounds staging
	auto* boundsStaging = new Buffer(_device, _boundsBuffer.Get().GetSize(),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::DEDICATED_HOST);
	boundsStaging->CopyBuffer(header.rawBounds, sizeof(header.rawBounds));

	_resourceManager.UnloadTexture(_sdfTexture);
	CreateSDFTexture(header.resolution);

	auto& texture = _sdfTexture.Get();

	SDFUploadJob job(_device, texture, _boundsBuffer.Get(), header.resolution,
		imageStaging, boundsStaging);
	CommandBuffer::ImmediateSubmit(_device, job);

	delete imageStaging;
	delete boundsStaging;

	_generated = true;
	return true;
}