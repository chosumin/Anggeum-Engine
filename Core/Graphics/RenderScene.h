#pragma once
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "Vulkans/BindlessTextureManager.h"
#include "RendererBatch.h"
#include "GeometryUpload.h"
#include "TextureUpload.h"

namespace Core
{
	class Device;
	class Scene;
	class TransferContext;

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
		RenderScene(Device& device, Scene& scene);

		Scene& GetScene() const { return _scene; }

		// Run every manager's self-gated sync, in dependency order.
		void Sync(Scene& scene, TransferContext& transfer, VkExtent2D extents);

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

		// Resource loading never issues transfer jobs itself; it pushes requests here
		// and Sync() turns them into jobs. Held by the coordinator until dedicated
		// residency managers exist.
		GeometryCopyQueue& GetGeometryCopyQueue() { return _geometryCopies; }
		TextureUploadQueue& GetTextureUploadQueue() { return _textureUploads; }

	private:
		void UploadQueuedTextures(TransferContext& transfer);
		void UploadQueuedGeometry(TransferContext& transfer);
		// Writes back what the upload jobs computed; only valid once they have finished.
		void ApplyComputedBounds();

		// A bounds result being computed by an in-flight upload job.
		struct PendingBounds
		{
			SubMesh* target;
			unique_ptr<GeometryBounds> result;
		};

	private:
		Scene& _scene;
		unique_ptr<BindlessTextureManager> _bindless;
		unique_ptr<MeshBufferManager> _meshBuffer;
		unique_ptr<MaterialManager> _material;
		unique_ptr<RendererBatch> _batch;

		TextureUploadQueue _textureUploads;
		GeometryCopyQueue _geometryCopies;
		vector<PendingBounds> _pendingBounds;

		glm::vec3 _sceneBoundsMin{ FLT_MAX };
		glm::vec3 _sceneBoundsMax{ -FLT_MAX };

		// Non-owning; null for the default (empty) instance, which never syncs.
		Device* _device = nullptr;
	};
}
