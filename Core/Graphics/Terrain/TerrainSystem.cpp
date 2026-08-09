#include "stdafx.h"
#include "TerrainSystem.h"
#include "Procedural/ProceduralTerrainHeightSource.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/ResourceManager.h"
#include "Utils/Math.h"
#include "Graphics/GeometryUpload.h"

namespace Core
{
	TerrainSystem::TerrainSystem(Device& device, GeometryCopyQueue& geometryCopyQueue)
		: _store(TerrainNodeStore::Load(_config,
			ProceduralTerrainHeightSource(TerrainNoiseParams{},
				_config.heightMin, _config.heightMax)))
	{
		_quadTree = make_unique<TerrainQuadTree>(device, _config);
		_streamer = make_unique<TerrainStreamer>(device, _config, _store, *_quadTree);
		CreateGridIndexBuffer(device, geometryCopyQueue);
	}

	void TerrainSystem::CreateGridIndexBuffer(Device& device,
		GeometryCopyQueue& geometryCopyQueue)
	{
		// One shared index buffer over a virtual (quadCountPerNodeEdge+1)^2 vertex grid;
		// terrain.vert derives positions from gl_VertexIndex, so there is no
		// vertex buffer at all.
		uint32_t quads = _config.quadCountPerNodeEdge;
		uint32_t verticesPerSide = quads + 1;
		assert(verticesPerSide * verticesPerSide <= 0x10000 && "u16 index space");

		vector<uint16_t> indices;
		indices.reserve(quads * quads * 6);
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

	void TerrainSystem::Update(PerspectiveCamera& camera)
	{
		vec3 position = camera.Matrices.Position;
		vec2 cameraXZ{ position.x, position.z };

		if (!_freezeStreaming)
			_streamer->Update(cameraXZ);
		else
			_streamer->ClearFrameUploads();

		BuildRenderList(cameraXZ, camera.GetProjection() * camera.GetView());
	}

	bool TerrainSystem::IsNodeVisible(const TerrainNodeId& id) const
	{
		const TerrainNodePayload& payload = _store.Get(id);
		float range = _config.heightMax - _config.heightMin;
		float size = _config.NodeSize(id.lod);
		// Coarse nodes bake min/max from decimated samples, so pad the height
		// bounds a little to stay conservative for the finer geometry below.
		const float heightPad = 2.0f;
		vec3 boundsMin(
			_config.WorldOrigin().x + float(id.x) * size,
			_config.heightMin + range * float(payload.minHeight) / 65535.0f - heightPad,
			_config.WorldOrigin().y + float(id.y) * size);
		vec3 boundsMax(boundsMin.x + size,
			_config.heightMin + range * float(payload.maxHeight) / 65535.0f + heightPad,
			boundsMin.z + size);

		for (const vec4& plane : _frustumPlanes)
		{
			vec3 positive(
				plane.x >= 0.0f ? boundsMax.x : boundsMin.x,
				plane.y >= 0.0f ? boundsMax.y : boundsMin.y,
				plane.z >= 0.0f ? boundsMax.z : boundsMin.z);
			if (dot(vec3(plane), positive) + plane.w < 0.0f)
				return false;
		}
		return true;
	}

	void TerrainSystem::VisitNode(const TerrainNodeId& id, vec2 cameraXZ)
	{
		if (!IsNodeVisible(id))
		{
			++_culledNodes;
			return;
		}

		// Refine with the same radii the streamer requests with, so the
		// rendered set tracks the requested set and fallback stays transient.
		bool canRefine = id.lod > 0
			&& DistanceToNodeXZ(cameraXZ, id) <= _streamer->LoadRadius(id.lod - 1);
		if (canRefine)
		{
			TerrainNodeId children[4];
			bool allResident = true;
			for (uint32_t i = 0; i < 4; ++i)
			{
				children[i] = { uint8_t(id.lod - 1),
					uint16_t(id.x * 2 + (i & 1)), uint16_t(id.y * 2 + (i >> 1)) };
				allResident &= _streamer->IsResident(children[i]);
			}

			if (allResident)
			{
				for (const TerrainNodeId& child : children)
					VisitNode(child, cameraXZ);
				return;
			}
		}

		if (!_streamer->IsResident(id))
			return; // only possible before the root finishes uploading

		uint16_t slot = _streamer->GetAtlasSlot(id);
		TerrainNodeInstance instance;
		instance.originXZ = _config.WorldOrigin()
			+ vec2(float(id.x), float(id.y)) * _config.NodeSize(id.lod);
		instance.sizeMeters = _config.NodeSize(id.lod);
		instance.lod = id.lod;
		instance.heightTexelOrigin = _quadTree->HeightTexelOrigin(slot);
		instance.colorTexelOrigin = _quadTree->ColorTexelOrigin(slot);
		_renderList.push_back(instance);
		++_renderListPerLod[std::min<uint32_t>(id.lod, 7)];
	}

	float TerrainSystem::DistanceToNodeXZ(vec2 point, const TerrainNodeId& id) const
	{
		float size = _config.NodeSize(id.lod);
		vec2 nodeMin = _config.WorldOrigin() + vec2(float(id.x), float(id.y)) * size;
		vec2 closest = clamp(point, nodeMin, nodeMin + size);
		return length(point - closest);
	}

	void TerrainSystem::BuildRenderList(vec2 cameraXZ, const mat4& viewProj)
	{
		_renderList.clear();
		_renderListPerLod = {};
		_culledNodes = 0;
		Math::ExtractFrustumPlanes(viewProj, _frustumPlanes.data());

		uint8_t rootLod = uint8_t(_config.lodCount - 1);
		for (uint16_t y = 0; y < _config.rootTilesZ; ++y)
			for (uint16_t x = 0; x < _config.rootTilesX; ++x)
				VisitNode({ rootLod, x, y }, cameraXZ);
	}

	void TerrainSystem::OnGUI()
	{
		const TerrainStreamingStats& stats = _streamer->GetStats();
		ImGui::Text("Nodes: %u requested, %u resident, %u pending, %u starved",
			stats.requested, stats.resident, stats.pending, stats.starved);
		ImGui::Text("This frame: %u uploaded, %u evicted, %u free slots",
			stats.uploadedThisFrame, stats.evictedThisFrame,
			_quadTree->GetFreeSlotCount());
		ImGui::Text("Drawn: %u nodes (LOD0 %u, LOD1 %u, LOD2 %u, LOD3 %u, LOD4 %u, LOD5 %u)",
			uint32_t(_renderList.size()), _renderListPerLod[0], _renderListPerLod[1],
			_renderListPerLod[2], _renderListPerLod[3], _renderListPerLod[4],
			_renderListPerLod[5]);
		ImGui::Text("Frustum culled: %u subtrees", _culledNodes);
		ImGui::Checkbox("Wireframe", &_wireframe);
		ImGui::Checkbox("Freeze streaming", &_freezeStreaming);
		ImGui::Combo("Debug mode", &_debugMode, "Lit\0LOD tint\0Normals\0UV grid\0");
	}
}
