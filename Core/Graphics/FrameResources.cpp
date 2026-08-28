#include "stdafx.h"
#include "FrameResources.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/Image.h"
#include "Vulkans/Texture.h"
#include "Vulkans/Shader.h"
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "ResourceManager.h"
#include "FrameGraph/TransientResourceAllocator.h"
#include "Foundation/Job.h"
#include "BufferUpload.h"
#include "RendererBatch.h"

using namespace Core;

FrameResources::FrameResources(Device& device, ResourceManager& resourceManager)
	: _device(device)
{
	CreateDescriptorPool();
	_defaultSampler = resourceManager.LoadSampler(DEFAULT_SAMPLER);
}

TransientResourceAllocator& FrameResources::GetTransientAllocator()
{
	if (_transientAllocator == nullptr)
		_transientAllocator = make_unique<TransientResourceAllocator>(_device);

	return *_transientAllocator;
}

FrameResources::~FrameResources()
{
	// RenderContext::Submit is the only thing that runs init work; anything still
	// pending here was queued after the last submit and would be silently dropped.
	assert(_pendingTransitions.empty() && AllInitJobsExecuted() &&
		"frame init work was queued but never executed");
}

void FrameResources::Reset()
{
	if (_descriptorPool)
		_descriptorPool->Reset();

	// Reached only after the GPU finished this slot's previous frame, so the
	// staging buffers these jobs own are no longer being read. Anything still
	// unexecuted was queued after Submit already ran, and dropping it here would
	// be silent, so catch it instead.
	assert(AllInitJobsExecuted() && "frame init work was queued but never executed");
	_pendingInitJobs.clear();
}

bool FrameResources::HasPendingInit() const
{
	// Reset() empties the job list at the top of the frame, so anything in it here
	// was queued by this frame's passes and has not run yet.
	return !_pendingTransitions.empty() || !_pendingInitJobs.empty();
}

void FrameResources::ExecutePendingInit(CommandBuffer& commandBuffer)
{
	if (!_pendingTransitions.empty())
	{
		auto barriers = commandBuffer.CreateBarrierBatch();
		for (auto& transition : _pendingTransitions)
		{
			barriers.Image(transition.texture.Get(),
				VK_IMAGE_LAYOUT_UNDEFINED, transition.targetLayout);
		}
		barriers.Submit();

		_pendingTransitions.clear();
	}

	// The jobs stay in the list rather than being consumed here: each owns the
	// staging buffer its copy reads from, which the GPU is still going to read.
	for (auto& job : _pendingInitJobs)
	{
		job->commandBuffer = &commandBuffer;
		job->Execute();
	}
}

bool FrameResources::AllInitJobsExecuted() const
{
	for (auto& job : _pendingInitJobs)
	{
		if (job->status != JobStatus::COMPLETE)
			return false;
	}

	return true;
}

void FrameResources::CreateDescriptorPool()
{
	_descriptorPool = make_unique<DescriptorPool>(_device);

	// Define pool sizes for different descriptor types
	vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 200 },
	};

	// Create pool with sufficient descriptor sets
	uint32_t maxSets = 1000;
	_descriptorPool->CreatePool(poolSizes, maxSets);
}

DescriptorSetBuilder FrameResources::CreateDescriptorSetBuilder(Shader& shader, uint32_t setIndex)
{
	return DescriptorSetBuilder(_device, *_descriptorPool, shader, setIndex);
}

Handle<Texture> FrameResources::GetRenderTarget(const string& name) const
{
	auto it = _renderTargets.find(name);
	if (it != _renderTargets.end())
		return it->second;
	return Handle<Texture>{};
}

Handle<Texture> FrameResources::GetOrCreateRenderTarget(const string& name,
	const RenderTargetDesc& desc)
{
	auto it = _renderTargets.find(name);
	if (it != _renderTargets.end())
		return it->second;

	return CreateRenderTarget(name, desc);
}

Handle<Texture> FrameResources::CreateRenderTarget(const string& name,
	const RenderTargetDesc& desc)
{
	assert(!IsRecordingGuardActive() && "resource pool mutation during the recording window");

	VkFormat format = desc.format;

	// Depth format fallback if undefined and aspect includes depth
	if (format == VK_FORMAT_UNDEFINED && desc.aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		format = _device.FindSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	}

	// Cube flag, view type, image type, and the CONCURRENT graphics+compute
	// sharing all come from Image::CreateImage.
	ImageDesc imageDesc = static_cast<const ImageDesc&>(desc);
	imageDesc.format = format; // depth fallback applied above

	auto image = make_unique<Image>(_device, imageDesc);
	auto* imagePtr = image.get();

	Handle<Sampler> sampler = desc.sampler.IsValid() ? desc.sampler : _defaultSampler;
	auto texture = make_shared<Texture>(name, std::move(image), sampler);

	// Render targets are what validation errors point at most of the time, so give
	// the layer a name to print instead of a bare handle.
	auto& debugUtils = _device.GetDebugUtils();
	debugUtils.SetObjectName(VK_OBJECT_TYPE_IMAGE,
		(uint64_t)imagePtr->GetImage(), name.c_str());
	debugUtils.SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW,
		(uint64_t)imagePtr->GetOrCreateImageView(0), (name + " View").c_str());

	Handle<Texture> handle = _renderTargetPool.Add(texture);
	_renderTargets[name] = handle;

	// Move the target from UNDEFINED into its requested starting layout. Deferred
	// into this frame's resource-init command buffer rather than submitted here:
	// that buffer runs on the graphics queue ahead of every pass in the frame, so
	// the target is in its starting layout before anything reads it, and creating
	// a target no longer costs a submit and a fence wait mid-recording.
	if (desc.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
		_pendingTransitions.push_back({ handle, desc.initialLayout });

	return handle;
}

Handle<Buffer> FrameResources::GetOrCreateStorageBuffer(const string& name,
	const BufferDesc& desc)
{
	auto it = _storageBufferHandles.find(name);
	if (it != _storageBufferHandles.end())
		return it->second;

	assert(!IsRecordingGuardActive() && "resource pool mutation during the recording window");

	auto buffer = make_shared<Buffer>(_device, desc.size, desc.usage, desc.memoryType);

	_device.GetDebugUtils().SetObjectName(VK_OBJECT_TYPE_BUFFER,
		(uint64_t)buffer->GetBuffer(), name.c_str());

	Handle<Buffer> handle = _bufferPool.Add(buffer);
	_storageBufferHandles[name] = handle;
	return handle;
}

Handle<Buffer> FrameResources::CreateOrReplaceStorageBuffer(const string& name,
	const BufferDesc& desc)
{
	assert(!IsRecordingGuardActive() && "resource pool mutation during the recording window");

	auto buffer = make_shared<Buffer>(_device, desc.size, desc.usage, desc.memoryType);

	_device.GetDebugUtils().SetObjectName(VK_OBJECT_TYPE_BUFFER,
		(uint64_t)buffer->GetBuffer(), name.c_str());

	auto it = _storageBufferHandles.find(name);
	if (it != _storageBufferHandles.end())
	{
		_bufferPool.Replace(it->second, buffer);
		return it->second;
	}

	Handle<Buffer> handle = _bufferPool.Add(buffer);
	_storageBufferHandles[name] = handle;
	return handle;
}

Handle<Buffer> FrameResources::CreateOrReplaceFilledStorageBuffer(const string& name,
	const BufferDesc& desc, vector<uint8_t>&& initialData)
{
	Handle<Buffer> handle = CreateOrReplaceStorageBuffer(name, desc);

	_pendingInitJobs.push_back(make_unique<BufferUploadJob<uint8_t>>(
		_device, handle.Get(), std::move(initialData), 0));

	return handle;
}

Handle<Buffer> FrameResources::GetOrCreateUniformBuffer(const string& name, VkDeviceSize size)
{
	auto it = _uniformBufferHandles.find(name);
	if (it != _uniformBufferHandles.end())
	{
		if (it->second.Get().GetSize() < size)
		{
			throw runtime_error(
				"FrameResources: uniform buffer '" + name + "' already exists at a smaller size.");
		}

		return it->second;
	}

	assert(!IsRecordingGuardActive() && "resource pool mutation during the recording window");

	auto buffer = make_shared<Buffer>(_device, size,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryType::UNIFORM);

	_device.GetDebugUtils().SetObjectName(VK_OBJECT_TYPE_BUFFER,
		(uint64_t)buffer->GetBuffer(), name.c_str());

	Handle<Buffer> handle = _bufferPool.Add(buffer);
	_uniformBufferHandles[name] = handle;
	return handle;
}
