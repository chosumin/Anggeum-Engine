#pragma once
#include "BufferObjects.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/ResourcePool.h"

namespace Core
{
	class ResourceManager;
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

	// Scene SDF volume + bounds.
	class SDFGenerator
	{
	public:
		SDFGenerator(Device& device, ResourceManager& resourceManager);
		~SDFGenerator();

		void Generate(FrameResources& frameResources, RenderFrame& renderFrame,
			uint32_t resolution = SDF_VOLUME_DIM);

		bool TryLoadFromFile(FrameResources& frameResources,
			uint32_t expectedResolution = SDF_VOLUME_DIM);

		void RequestSave(FrameResources& frameResources, uint32_t resolution = SDF_VOLUME_DIM);
		void FinishSave(FrameResources& frameResources);

		void SetCachePath(const std::string& path) { _sdfCachePath = path; }
		const std::string& GetCachePath() const { return _sdfCachePath; }

		Handle<Texture> GetSDFTexture() const { return _sdfTexture; }
		Buffer* GetBoundsBuffer() const { return &_boundsBuffer.Get(); }
		bool IsGenerated() const { return _generated; }

	private:
		class GenerateJob;

		void CreateSDFTexture(uint32_t resolution);
		void EnsureTriangleLookup(uint32_t totalTriangles);
		void RecordGenerate(FrameResources& frameResources, RenderFrame& renderFrame,
			CommandBuffer& commandBuffer, uint32_t resolution);
		void ComputeWorldBounds(FrameResources& frameResources, CommandBuffer& commandBuffer,
			Buffer& instanceDataBuffer, Buffer& transformBuffer,
			uint32_t instanceCount);
		void BuildTriangleLookup(FrameResources& frameResources, CommandBuffer& commandBuffer,
			Buffer& instanceDataBuffer, Buffer& drawCommandBuffer,
			uint32_t drawCommandCount, uint32_t totalTriangles);

	private:
		Device& _device;
		ResourceManager& _resourceManager;

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

		// Pending save: the readback queued on _saveFrame fills these.
		FrameResources* _saveFrame = nullptr;
		unique_ptr<Buffer> _saveImageStaging;
		unique_ptr<Buffer> _saveBoundsStaging;
		uint32_t _saveResolution = 0;

		std::string _sdfCachePath = "Assets/Cache/sdf_volume.sdfvol";
		float _paddingFactor = 0.1f;
		bool _generated = false;
	};
}
