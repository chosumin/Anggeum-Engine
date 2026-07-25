#pragma once
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/MemoryAllocator.h"
#include "ResourcePool.h"
#include "FrameResources.h"
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "IndirectDrawBuffer.h"
#include "RenderExecutor.h"
#include "Vulkans/SubmitInfo.h"

namespace Core
{
	class CommandBuffer;
	class CommandPool;
	class Material;
	class Shader;
	class Texture;
	class Buffer;
	class BindlessTextureManager;
	class IndirectDrawBuffer;
	class RenderPass;
	class Framebuffer;
	class DescriptorSetBuilder;
	class RendererBatch;
	class PipelineState;
	class Scene;

	class RenderFrame
	{
	public:
		RenderFrame(Device& device, BindlessTextureManager* bindlessManager = nullptr);
		~RenderFrame();
		
		// Reset frame resources
		void Reset();

		// Submit info management
		SubmitInfo& AddSubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext);
		SubmitInfo& GetCurrentSubmitInfo() { return _submission.submitInfos.back(); }
		FrameSubmission& GetSubmission() { return _submission; }

		BindlessTextureManager* GetBindlessTextureManager() const { return _bindlessTextureManager; }
		bool HasBindlessSupport() const { return _bindlessTextureManager != nullptr; }
		DescriptorSetResources* GetBindlessResources();

		void SetMeshBufferManager(MeshBufferManager* meshBufferManager) { _meshBufferManager = meshBufferManager; }
		MeshBufferManager* GetMeshBufferManager() const { return _meshBufferManager; }

		void SetMaterialManager(MaterialManager* materialManager) { _materialManager = materialManager; }
		MaterialManager* GetMaterialManager() { return _materialManager; }

		// Per-frame GPU resources (render targets, transient buffers, framebuffers)
		// live in FrameResources. Passes reach them through here.
		FrameResources& GetResources() { return _resources; }
		const FrameResources& GetResources() const { return _resources; }

		// Culler management - per camera and RendererBatch, reused within a frame
		RenderExecutor& GetRenderExecutor() { return *_renderExecutor; }

		// Batch initialization - called once at the start of rendering
		void InitializeBatches(Scene& scene, VkExtent2D extents);
	private:
		void CreateSyncObjects();

	private:
		Device& _device;

		FrameSubmission _submission;

		BindlessTextureManager* _bindlessTextureManager;
		DescriptorSetResources _bindlessResources;

		// GPU Driven Rendering Buffers
		MeshBufferManager* _meshBufferManager = nullptr;
		MaterialManager* _materialManager = nullptr;

		// Per-frame GPU resources (render targets / transient buffers / framebuffers)
		FrameResources _resources;

		// Per-camera/RendererBatch cullers, reused within a frame
		unique_ptr<RenderExecutor> _renderExecutor;
	};
}