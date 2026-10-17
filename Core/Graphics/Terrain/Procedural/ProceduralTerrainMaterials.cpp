#include "stdafx.h"
#include "ProceduralTerrainMaterials.h"
#include "../TerrainMaterialSet.h"
#include "../TerrainUpload.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Texture.h"
#include "Utils/Log.h"

namespace
{
	using namespace Core;

	constexpr uint32_t kDetailTextureSize = 256;

	// A baked chain becomes a mipmapped texture, registered bindless (born
	// Resident, so the descriptor is written at the next Sync) and queued
	// for the one upload that fills it before any draw.
	Handle<Texture> CreateDetailTexture(Device& device, ResourceManager& resourceManager,
		BindlessTextureManager* bindless, Handle<Sampler> sampler, const string& name,
		VkFormat format, TerrainDetailMipChain&& chain, vector<TerrainDetailUpload>& uploads)
	{
		// Square power-of-two chains: mip 0 edge from the chain length.
		const uint32_t size = 1u << (uint32_t(chain.mips.size()) - 1);
		assert(chain.mips[0].size() == size_t(size) * size * chain.bytesPerTexel);

		ImageDesc desc{};
		desc.extent = { size, size };
		desc.format = format;
		desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		desc.mipLevels = uint32_t(chain.mips.size());

		Handle<Texture> handle = resourceManager.LoadTexture(name,
			make_unique<Image>(device, desc), sampler);
		if (bindless != nullptr)
			handle.Get().SetBindlessIndex(bindless->RegisterTexture(handle));

		TerrainDetailUpload upload;
		upload.texture = handle;
		upload.size = size;
		upload.bytesPerTexel = chain.bytesPerTexel;
		upload.mips = std::move(chain.mips);
		uploads.push_back(std::move(upload));
		return handle;
	}
}

namespace Core
{
	vector<TerrainProceduralMaterial> ProceduralTerrainMaterialPalette()
	{
		return {
			{ { "Grass", 3.0f,  0.0f }, { { 0.22f, 0.34f, 0.12f }, 0.95f }, { 0.30f, 0.40f, 0.14f }, 0.25f, 7.0f, 11u },
			{ { "Dirt",  4.0f, 23.0f }, { { 0.32f, 0.26f, 0.18f }, 0.90f }, { 0.40f, 0.30f, 0.20f }, 0.35f, 5.0f, 23u },
			{ { "Rock",  6.0f, 47.0f }, { { 0.42f, 0.40f, 0.38f }, 0.70f }, { 0.30f, 0.29f, 0.30f }, 0.60f, 4.0f, 47u },
			{ { "Snow",  5.0f, 71.0f }, { { 0.90f, 0.92f, 0.95f }, 0.55f }, { 0.84f, 0.88f, 0.94f }, 0.15f, 3.0f, 71u },
		};
	}

	vector<TerrainMaterialBakeDesc> ProceduralTerrainMaterialBakeDescs()
	{
		vector<TerrainMaterialBakeDesc> descs;
		for (const TerrainProceduralMaterial& material : ProceduralTerrainMaterialPalette())
			descs.push_back(material.bake);
		return descs;
	}

	unique_ptr<TerrainMaterialSet> CreateProceduralTerrainMaterials(Device& device,
		ResourceManager& resourceManager, BindlessTextureManager* bindless)
	{
		auto start = chrono::high_resolution_clock::now();

		// World-space tiling: repeat, trilinear.
		Handle<Sampler> sampler = resourceManager.LoadSampler(
			{ VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			  VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			  VK_SAMPLER_MIPMAP_MODE_LINEAR });

		vector<TerrainMaterialDesc> descs;
		vector<TerrainMaterialTextures> textures;
		vector<TerrainDetailUpload> uploads;
		for (const TerrainProceduralMaterial& material : ProceduralTerrainMaterialPalette())
		{
			TerrainDetailMaps maps = TerrainDetailBaker::Bake(material, kDetailTextureSize);
			string base = string("Terrain.Detail.") + material.material.name;

			TerrainMaterialTextures set;
			set.albedo = CreateDetailTexture(device, resourceManager, bindless, sampler,
				base + ".Albedo", VK_FORMAT_R8G8B8A8_UNORM, std::move(maps.albedo), uploads);
			set.normal = CreateDetailTexture(device, resourceManager, bindless, sampler,
				base + ".Normal", VK_FORMAT_R8G8B8A8_UNORM, std::move(maps.normal), uploads);
			set.height = CreateDetailTexture(device, resourceManager, bindless, sampler,
				base + ".Height", VK_FORMAT_R8_UNORM, std::move(maps.height), uploads);

			descs.push_back(material.material);
			textures.push_back(set);
		}

		auto elapsed = chrono::duration_cast<chrono::milliseconds>(
			chrono::high_resolution_clock::now() - start).count();
		LOG("Terrain materials: %u procedural detail sets (%u^2) baked in %lld ms",
			uint32_t(descs.size()), kDetailTextureSize, elapsed);

		vector<unique_ptr<Job>> initJobs;
		initJobs.push_back(make_unique<TerrainDetailUploadJob>(device, std::move(uploads)));
		return make_unique<TerrainMaterialSet>(std::move(descs), std::move(textures),
			std::move(initJobs));
	}
}
