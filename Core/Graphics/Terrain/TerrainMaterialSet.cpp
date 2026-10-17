#include "stdafx.h"
#include "TerrainMaterialSet.h"
#include "Foundation/Job.h"
#include "Graphics/FrameResources.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/Texture.h"

namespace Core
{
	TerrainMaterialSet::TerrainMaterialSet(vector<TerrainMaterialDesc>&& materials,
		vector<TerrainMaterialTextures>&& textures, vector<unique_ptr<Job>>&& initJobs)
		: _materials(std::move(materials))
		, _textures(std::move(textures))
		, _initJobs(std::move(initJobs))
	{
		assert(_materials.size() <= TERRAIN_MAX_MATERIALS);
		assert(_textures.size() == _materials.size());
	}

	TerrainMaterialSet::~TerrainMaterialSet() = default;

	TerrainMaterialPaletteGPU TerrainMaterialSet::BuildPalette() const
	{
		TerrainMaterialPaletteGPU palette{};
		for (size_t i = 0; i < _materials.size(); ++i)
		{
			const TerrainMaterialDesc& material = _materials[i];
			const TerrainMaterialTextures& textures = _textures[i];
			float rotation = glm::radians(material.rotationDegrees);

			TerrainMaterialGPU& gpu = palette.materials[i];
			gpu.textures = uvec4(textures.albedo.Get().GetBindlessIndex(),
				textures.normal.Get().GetBindlessIndex(),
				textures.height.Get().GetBindlessIndex(), 0u);
			gpu.tiling = vec4(1.0f / std::max(material.tileSize, 0.01f),
				cos(rotation), sin(rotation), 0.0f);
		}
		palette.count = uvec4(uint32_t(_materials.size()), 0u, 0u, 0u);
		return palette;
	}

	void TerrainMaterialSet::QueuePendingInit(FrameResources& frameResources)
	{
		for (auto& job : _initJobs)
			frameResources.AddInitJob(std::move(job));
		_initJobs.clear();
	}

	void TerrainMaterialSet::OnGUI()
	{
		for (size_t i = 0; i < _materials.size(); ++i)
		{
			TerrainMaterialDesc& material = _materials[i];
			const TerrainMaterialTextures& textures = _textures[i];
			ImGui::PushID(int(i));
			if (ImGui::TreeNode(material.name))
			{
				ImGui::SliderFloat("Tile size", &material.tileSize, 0.5f, 32.0f, "%.1f m");
				ImGui::SliderFloat("Rotation", &material.rotationDegrees, 0.0f, 180.0f, "%.0f deg");
				ImGui::Text("Bindless: albedo %u, normal %u, height %u",
					textures.albedo.Get().GetBindlessIndex(),
					textures.normal.Get().GetBindlessIndex(),
					textures.height.Get().GetBindlessIndex());
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
}
