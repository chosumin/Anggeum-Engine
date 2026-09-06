#pragma once
#include "TerrainNode.h"
#include "TerrainConfig.h"
#include "Graphics/UploadJob.h"
#include "Graphics/StagingRing.h"

namespace Core
{
	class TerrainNodeStore;
	class TerrainQuadTree;
	class Texture;
	class Buffer;

	// One tile the streamer admitted this frame: which node's payload goes
	// into which atlas slot.
	struct TerrainTileUpload
	{
		TerrainNodeId id;
		uint16_t slot;
	};

	class TerrainUploadJob : public UploadJob
	{
	public:
		TerrainUploadJob(TerrainQuadTree& quadTree, const TerrainNodeStore& store,
			const TerrainConfig& config, vector<TerrainTileUpload>&& tiles,
			StagingRing::Span span, bool initializeAtlases);
		~TerrainUploadJob();

		void Execute() override;

	private:
		const TerrainNodeStore& _store;
		const TerrainConfig& _config;

		Texture& _heightAtlas;
		Texture& _normalAtlas;
		Texture& _albedoAtlas;

		// Slot -> texel origin mapping, resolved on the main thread.
		TerrainQuadTree& _quadTree;

		vector<TerrainTileUpload> _tiles;
		StagingRing::Span _span;
		bool _initializeAtlases;
	};

	class TerrainIndexUploadJob : public Job
	{
	public:
		TerrainIndexUploadJob(Device& device, Texture& indexTexture,
			const TerrainConfig& config, vector<vector<uint16_t>>&& indexMips);
		~TerrainIndexUploadJob();

		void Execute() override;

	private:
		Device& _device;
		const TerrainConfig& _config;

		Texture& _indexTexture;
		vector<vector<uint16_t>> _indexMips;

		unique_ptr<Buffer> _stagingBuffer;
	};
}
