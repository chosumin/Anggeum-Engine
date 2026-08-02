#pragma once
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/MemoryAllocator.h"
#include "ResourcePool.h"
#include "FrameResources.h"
#include "RenderScene.h"
#include "Vulkans/SubmitInfo.h"

namespace Core
{
	class BindlessTextureManager;
	class DescriptorSetBuilder;
	class RendererBatch;
	class RenderScene;
	class CommandBuffer;
	class Shader;
	class Pipeline;

	class RenderFrame
	{
	public:
		RenderFrame(Device& device, RenderScene& renderScene);
		~RenderFrame();
		
		// Reset frame resources
		void Reset();

		// Submit info management
		SubmitInfo& AddSubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext);
		SubmitInfo& GetCurrentSubmitInfo() { return _submission.submitInfos.back(); }
		FrameSubmission& GetSubmission() { return _submission; }

		BindlessTextureManager* GetBindlessTextureManager() const { return _renderScene.GetBindlessTextureManager(); }
		bool HasBindlessSupport() const { return _renderScene.HasBindlessSupport(); }
		DescriptorSetResources* GetBindlessResources();

		MeshBufferManager& GetMeshBufferManager() const { return *_renderScene.GetMeshBufferManager(); }

		MaterialManager& GetMaterialManager() { return *_renderScene.GetMaterialManager(); }

		// Per-frame GPU resources (render targets, transient buffers, framebuffers)
		// live in FrameResources. Passes reach them through here.
		FrameResources& GetResources() { return _resources; }
		const FrameResources& GetResources() const { return _resources; }

		// Records one indirect draw of the scene batch. Lives here because the
		// vertex/index, transform, material and bindless inputs it binds are all
		// reached through this frame.
		void DrawIndirect(CommandBuffer& commandBuffer,
			Shader& shader, Pipeline& pipeline, Buffer& indirectCommandBuffer,
			DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook);

		// RendererBatch is owned by RenderContext and shared across frames-in-flight.
		RendererBatch& GetRendererBatch() const { return *_renderScene.GetRendererBatch(); }
	private:
		void CreateSyncObjects();

	private:
		Device& _device;

		FrameSubmission _submission;

		// Non-owning: the RenderScene owned by Engine.
		RenderScene& _renderScene;

		DescriptorSetResources _bindlessResources;

		// Per-frame GPU resources (render targets / transient buffers / cullers)
		FrameResources _resources;
	};
}
