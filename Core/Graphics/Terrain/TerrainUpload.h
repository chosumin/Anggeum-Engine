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

	// Uploads a frame's terrain streaming work - tile payloads into the
	// synchronized atlases plus (when dirty) the quadtree lookup tables - as
	// ONE job, so GPU-visible residency stays atomic.
	class TerrainUploadJob : public UploadJob
	{
	public:
		// Texture/buffer handles resolve at construction; 
		// the table snapshots are COPIES, because the streamer mutates its mirrors
		// next frame while this job may still be recording. 
		// The staging span was acquired by the streamer BEFORE it committed
		// any bookkeeping, so a full ring skips the whole frame instead of
		// leaving tables that point at tiles that never uploaded.
		TerrainUploadJob(TerrainQuadTree& quadTree, const TerrainNodeStore& store,
			const TerrainConfig& config, vector<TerrainTileUpload>&& tiles,
			bool tablesDirty, vector<TerrainNodeDescGPU>&& descSnapshot,
			vector<vector<uint16_t>>&& indexSnapshot, bool firstUpload,
			StagingRing::Span span);
		~TerrainUploadJob();

		void Execute() override;

	private:
		const TerrainNodeStore& _store;
		const TerrainConfig& _config;

		Texture& _heightAtlas;
		Texture& _normalAtlas;
		Texture& _albedoAtlas;
		Texture& _indexTexture;
		Buffer& _nodeDescBuffer;

		// Slot -> texel origin mapping, resolved on the main thread.
		TerrainQuadTree& _quadTree;

		vector<TerrainTileUpload> _tiles;
		bool _tablesDirty;
		vector<TerrainNodeDescGPU> _descSnapshot;
		vector<vector<uint16_t>> _indexSnapshot;

		// First upload ever: the images are still UNDEFINED, and ALL of them
		// must leave initialized (the graph believes SHADER_READ_ONLY from
		// frame 0, even for atlases no tile has touched yet).
		bool _firstUpload;

		StagingRing::Span _span;
	};
}
