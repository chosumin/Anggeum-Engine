#pragma once
#include "BufferObjects.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/ResourcePool.h"

namespace Core
{
	class Device;
	class Image;
	class Texture;
	class Sampler;
	class Buffer;
	class MeshBufferManager;
	class Pipeline;
	class Shader;
	class CommandBuffer;
	class FrameResources;
	class RenderFrame;

	struct SDFGeneratePushConstants
	{
		uint32_t resolution;
		uint32_t triangleCount;
		float paddingFactor;
		uint32_t useUint16Indices;
	};

	class SDFGenerator
	{
	public:
		SDFGenerator(Device& device);
		~SDFGenerator();

		// Records the full generation (bounds reduce, triangle lookup, volume
		// dispatch) into the given command buffer. Creates/resizes pool-owned
		// resources, so it must run on the main thread (e.g. a single-time
		// command buffer submitted outside the frame graph's recording window).
		void Generate(FrameResources& frameResources, RenderFrame& renderFrame,
			CommandBuffer& commandBuffer,
			uint32_t resolution = SDF_VOLUME_DIM);

		// Persistent storage. File format includes the bounds buffer so the
		// loaded volume's coordinate system matches generation time.
		bool TryLoadFromFile(uint32_t expectedResolution = SDF_VOLUME_DIM);
		bool SaveToFile(uint32_t resolution = SDF_VOLUME_DIM);

		void SetCachePath(const std::string& path) { _sdfCachePath = path; }
		const std::string& GetCachePath() const { return _sdfCachePath; }

		Handle<Texture> GetSDFTexture() const { return _sdfTexture; }
		Buffer* GetBoundsBuffer() const { return &_boundsBuffer.Get(); }
		bool IsGenerated() const { return _generated; }

	private:
		void CreateSDFTexture(uint32_t resolution);
		void ComputeWorldBounds(FrameResources& frameResources, CommandBuffer& commandBuffer,
			Buffer& objectDataBuffer, Buffer& transformBuffer,
			uint32_t instanceCount);
		void BuildTriangleLookup(FrameResources& frameResources, CommandBuffer& commandBuffer,
			Buffer& objectDataBuffer, Buffer& drawCommandBuffer,
			uint32_t drawCommandCount, uint32_t totalTriangles);

	private:
		Device& _device;

		// GPU-generated volume texture. App-lifetime, so it lives in the
		// ResourceManager texture pool; this generator just holds the handle.
		Handle<Texture> _sdfTexture;
		Handle<Shader> _sdfGenerateShader;
		Handle<Pipeline> _sdfGeneratePipeline;

		// Bounds/triLookup are app-lifetime, so they live in the ResourceManager global
		// buffer pool (via CreateBuffer/ResizeBuffer); held here by handle.
		Handle<Shader> _boundsReduceShader;
		Handle<Pipeline> _boundsReducePipeline;
		Handle<Buffer> _boundsBuffer;

		// Per-triangle lookup: stores vertexOffset and transformIndex for each triangle
		Handle<Shader> _triLookupShader;
		Handle<Pipeline> _triLookupPipeline;
		Handle<Buffer> _triLookupBuffer;

		std::string _sdfCachePath = "Assets/Cache/sdf_volume.sdfvol";
		float _paddingFactor = 0.1f;
		bool _generated = false;
	};
}