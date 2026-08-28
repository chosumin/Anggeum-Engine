#include "stdafx.h"
#include "TerrainSystem.h"
#include "Procedural/ProceduralTerrainHeightSource.h"
#include "Foundation/Entity.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Transform.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/GeometryUpload.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/TransferContext.h"
#include "Graphics/Vulkans/Buffer.h"

namespace Core
{
	TerrainSystem::TerrainSystem(Device& device, TransferContext& transfer)
		: _store(TerrainNodeStore::Load(_config,
			ProceduralTerrainHeightSource(TerrainNoiseParams{},
				_config.heightMin, _config.heightMax)))
	{
		_quadTree = make_unique<TerrainQuadTree>(device, _config);
		_streamer = make_unique<TerrainStreamer>(_config, _store, *_quadTree, transfer);
		CreateGridIndexBuffer(device, transfer.GetGeometryCopyQueue());

		// Zero-initialized: fresh device memory is undefined, and OnGUI reads
		// each slot before its first GPU write has happened.
		auto& resourceManager = device.GetResourceManager();
		for (uint32_t slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot)
		{
			_patchCountReadback[slot] = resourceManager.LoadBuffer(
				{ sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::UNIFORM },
				"Terrain.PatchCountReadback" + to_string(slot));

			uint32_t zero = 0;
			_patchCountReadback[slot].Get().Update(zero);
		}
	}

	void TerrainSystem::CreateGridIndexBuffer(Device& device,
		GeometryCopyQueue& geometryCopyQueue)
	{
		// One shared index buffer over a virtual 17x17 PATCH grid - the draw
		// instance is one patch of the GPU-culled patch list; terrain.vert
		// derives positions from gl_VertexIndex, so there is no vertex buffer.
		uint32_t quads = _config.PatchQuads();
		uint32_t verticesPerSide = quads + 1;
		assert(verticesPerSide * verticesPerSide <= 0x10000 && "u16 index space");
		assert(_config.quadCountPerNodeEdge % _config.patchesPerNodeEdge == 0);

		vector<uint16_t> indices;
		indices.reserve(size_t(quads) * quads * 6);
		for (uint32_t y = 0; y < quads; ++y)
			for (uint32_t x = 0; x < quads; ++x)
			{
				uint16_t v0 = uint16_t(y * verticesPerSide + x);
				uint16_t v1 = uint16_t(v0 + 1);
				uint16_t v2 = uint16_t(v0 + verticesPerSide);
				uint16_t v3 = uint16_t(v2 + 1);
				indices.insert(indices.end(), { v0, v2, v1, v1, v2, v3 });
			}
		_gridIndexCount = uint32_t(indices.size());
		assert(_gridIndexCount == _config.PatchIndexCount());

		_gridIndexBuffer = device.GetResourceManager().LoadBuffer(
			{ indices.size() * sizeof(uint16_t),
			  VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			  MemoryType::DEVICE_LOCAL },
			"Terrain.GridIndices");

		GeometryCopyBatch batch;
		batch.debugName = "Terrain.GridIndices";
		const auto* bytes = reinterpret_cast<const uint8_t*>(indices.data());
		batch.copies.push_back({ _gridIndexBuffer,
			vector<uint8_t>(bytes, bytes + indices.size() * sizeof(uint16_t)), 0 });
		geometryCopyQueue.Push(move(batch));
	}

	TerrainParams TerrainSystem::BuildRenderParams(Light* mainLight) const
	{
		TerrainParams params{};
		params.heightMinMaxInvAtlas = vec4(_config.heightMin, _config.heightMax,
			1.0f / vec2(_quadTree->GetHeightAtlasExtent()));
		params.invColorAtlasBorder = vec4(1.0f / vec2(_quadTree->GetColorAtlasExtent()),
			float(_config.borderTexels), 0.0f);

		if (mainLight)
		{
			auto& transform = mainLight->GetEntity().GetTransform();
			vec3 direction = transform.GetRotation() * mainLight->GetProperties().Direction;
			if (length(direction) > 0.0f)
				params.sunDirection = vec4(normalize(direction), 0.25f);
		}
		params.debugMode = ivec4(_debugMode, 0, 0, 0);
		params.worldParams = vec4(_config.WorldOrigin(), _config.rootNodeSize,
			float(_config.lodCount));
		params.atlasInfo = vec4(float(_config.atlasSlotsPerRow),
			float(_config.HeightTexels()), float(_config.ColorTexels()), 0.0f);
		return params;
	}

	void TerrainSystem::Update(PerspectiveCamera& camera)
	{
		vec3 position = camera.Matrices.Position;
		vec2 cameraXZ{ position.x, position.z };

		if (!_freezeStreaming)
			_streamer->Update(cameraXZ);
	}

	void TerrainSystem::OnGUI()
	{
		const TerrainStreamingStats& stats = _streamer->GetStats();
		ImGui::Text("Nodes: %u requested, %u resident, %u pending, %u starved",
			stats.requested, stats.resident, stats.pending, stats.starved);
		ImGui::Text("This frame: %u uploaded, %u evicted, %u free slots",
			stats.uploadedThisFrame, stats.evictedThisFrame,
			_quadTree->GetFreeSlotCount());
		uint32_t slot = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);
		uint32_t patchCount = 0;
		void* mapped = nullptr;
		_patchCountReadback[slot].Get().GetMappedPtr(&mapped);
		memcpy(&patchCount, mapped, sizeof(patchCount));
		ImGui::Text("Visible patches (GPU, 2f delay): %u", patchCount);
		ImGui::Checkbox("Wireframe", &_wireframe);
		ImGui::Checkbox("Freeze streaming", &_freezeStreaming);
		ImGui::Combo("Debug mode", &_debugMode, "Lit\0LOD tint\0Normals\0UV grid\0");
	}
}
