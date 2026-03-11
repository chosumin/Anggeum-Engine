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
		uint32_t resolution;
		uint32_t triangleCount;
		float paddingFactor;
	};

	class SDFGenerator
	{
	public:
		SDFGenerator(Device& device);
		~SDFGenerator();

		void Generate(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			MeshBufferManager& meshBufferManager,
			Buffer* objectDataBuffer, Buffer* transformBuffer,
			uint32_t instanceCount,
			uint32_t resolution = SDF_VOLUME_DIM);

		shared_ptr<Texture> GetSDFTexture() const { return _sdfTexture; }
		Buffer* GetBoundsBuffer() const { return _boundsBuffer; }
		bool IsGenerated() const { return _generated; }

	private:
		void CreateSDFTexture(uint32_t resolution);
		void ComputeWorldBounds(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
			Buffer* objectDataBuffer, Buffer* transformBuffer,
			uint32_t instanceCount);

	private:
		Device& _device;

		shared_ptr<Texture> _sdfTexture;
		shared_ptr<Shader> _sdfGenerateShader;
		unique_ptr<Pipeline> _sdfGeneratePipeline;

		// Bounds reduction (single pass, no finalize)
		shared_ptr<Shader> _boundsReduceShader;
		unique_ptr<Pipeline> _boundsReducePipeline;
		Buffer* _boundsBuffer = nullptr; // 8 uints: encoded min/max

		float _paddingFactor = 0.1f;
		bool _generated = false;
	};
}