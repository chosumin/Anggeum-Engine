#pragma once
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "Vulkans/BindlessTextureManager.h"
#include "RendererBatch.h"

namespace Core
{
	class Device;
	class Scene;
	class TransferContext;
	class CommandBuffer;
	class Shader;
	class Pipeline;
	class Buffer;
	class DescriptorSetBuilder;
	class TerrainSystem;
	class UploadScheduler;
	class GeometryCopyQueue;
	class TextureUploadQueue;

	// RenderScene: the GPU mirror of the scene used for GPU-driven rendering (bindless
	// textures, mesh buffers, material table, draw batch). Owned by Engine and shared by
	// reference with RenderContext / RenderFrames.
	//
	// It is the coordinator, not a manager itself: every manager follows the same dirty
	// pattern — it owns its pending state and its Sync() is a no-op when clean — and
	// RenderScene::Sync() only sequences them (geometry upload before draw-set rebuild).
	// Dirty states are independent axes:
	//   Bindless        — pending descriptor writes
	//   Material        — dirty table entries
	//   MeshBuffer      — non-empty geometry upload queue (geometry residency)
	//   RendererBatch   — the scene's draw-set dirty flag (membership changes)
	// so geometry can stream in without a batch rebuild, and instancing changes can
	// rebuild the batch without touching geometry.
	class RenderScene
	{
	public:
		// The CPU scene this render scene mirrors. The scene object is created
		// first (empty) and loaded later, so it can be a constructor argument.
		RenderScene(Device& device, Scene& scene, UploadScheduler& uploads);
		~RenderScene();

		Scene& GetScene() const { return _scene; }

		// The terrain world system: part of the renderer's world model, created
		// with the other managers and updated by Sync() like them.
		TerrainSystem& GetTerrainSystem() const { return *_terrainSystem; }

		// Run every manager's self-gated sync, in dependency order.
		void Sync(Scene& scene, VkExtent2D extents);

		// Null when descriptor indexing is unsupported.
		BindlessTextureManager* GetBindlessTextureManager() const { return _bindless.get(); }
		bool HasBindlessSupport() const { return _bindless != nullptr; }

		MeshBufferManager* GetMeshBufferManager() const { return _meshBuffer.get(); }
		MaterialManager* GetMaterialManager() const { return _material.get(); }
		RendererBatch* GetRendererBatch() const { return _batch.get(); }

		// Local-space bounds covering every mesh registered so far, grown as the upload
		// jobs report what they measured.
		const glm::vec3& GetSceneBoundsMin() const { return _sceneBoundsMin; }
		const glm::vec3& GetSceneBoundsMax() const { return _sceneBoundsMax; }

		// Records one indirect draw of the scene batch: binds the global mesh
		// buffers, the batch's transform/instance/material tables and (when the
		// shader wants it) the bindless set. Lives here because every one of
		// those inputs is owned by this class.
		void DrawIndirect(CommandBuffer& commandBuffer, Shader& shader,
			Pipeline& pipeline, Buffer& indirectCommandBuffer,
			DescriptorSetBuilder& builder);

		GeometryCopyQueue& GetGeometryCopyQueue();
		TextureUploadQueue& GetTextureUploadQueue();

	private:
		// Writes back what the upload jobs computed; only valid once they have finished.
		void ApplyComputedBounds();

	private:
		Scene& _scene;
		UploadScheduler& _uploads;
		unique_ptr<BindlessTextureManager> _bindless;
		unique_ptr<MeshBufferManager> _meshBuffer;
		unique_ptr<MaterialManager> _material;
		unique_ptr<RendererBatch> _batch;
		unique_ptr<TerrainSystem> _terrainSystem;

		glm::vec3 _sceneBoundsMin{ FLT_MAX };
		glm::vec3 _sceneBoundsMax{ -FLT_MAX };

		// Non-owning; null for the default (empty) instance, which never syncs.
		Device* _device = nullptr;
	};
}
