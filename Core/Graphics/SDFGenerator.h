#pragma once
#include "BufferObjects.h"

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
	class RenderFrame;

	struct SDFGeneratePushConstants
	{
		glm::vec4 volumeMin;
		glm::vec4 volumeMax;
		uint32_t resolution;
		uint32_t triangleCount;
		uint32_t padding[2];
	};

	class SDFGenerator
	{
	public:
		SDFGenerator(Device& device);
		~SDFGenerator() = default;

		/// Generate SDF volume on the GPU.
		/// Bounds are taken from MeshBufferManager's accumulated scene bounds.
		void Generate(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			MeshBufferManager& meshBufferManager,
			uint32_t resolution = SDF_VOLUME_DIM);

		shared_ptr<Texture> GetSDFTexture() const { return _sdfTexture; }
		const glm::vec3& GetBoundsMin() const { return _boundsMin; }
		const glm::vec3& GetBoundsMax() const { return _boundsMax; }
		bool IsGenerated() const { return _generated; }

	private:
		void CreateSDFTexture(uint32_t resolution);

	private:
		Device& _device;

		shared_ptr<Texture> _sdfTexture;
		shared_ptr<Shader> _sdfGenerateShader;
		unique_ptr<Pipeline> _sdfGeneratePipeline;

		glm::vec3 _boundsMin{0.0f};
		glm::vec3 _boundsMax{0.0f};
		bool _generated = false;
	};
}