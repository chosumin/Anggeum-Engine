#pragma once
#include "BufferObjects.h"

namespace Core
{
	class Device;
	class Mesh;
	class Image;
	class Texture;
	class Sampler;

	struct SDFVolumeData
	{
		std::vector<float> distanceField;
		glm::vec3 boundsMin;
		glm::vec3 boundsMax;
		uint32_t resolution;
	};

	class SDFGenerator
	{
	public:
		SDFGenerator(Device& device);
		~SDFGenerator() = default;

		/// Generate an SDF volume from scene meshes.
		/// Returns a 3D texture containing signed distances.
		shared_ptr<Texture> Generate(const std::vector<Mesh*>& meshes,
			uint32_t resolution = SDF_VOLUME_DIM);

		const glm::vec3& GetBoundsMin() const { return _boundsMin; }
		const glm::vec3& GetBoundsMax() const { return _boundsMax; }

	private:
		/// Compute the scene AABB from all mesh bounding boxes
		void ComputeSceneBounds(const std::vector<Mesh*>& meshes);

		/// Calculate the unsigned distance from a point to the nearest triangle
		float DistanceToMeshes(const glm::vec3& point,
			const std::vector<Mesh*>& meshes) const;

		/// Upload the 3D float data into a Vulkan 3D image
		shared_ptr<Texture> Upload3DTexture(const std::vector<float>& data,
			uint32_t resolution);

	private:
		Device& _device;
		glm::vec3 _boundsMin{0.0f};
		glm::vec3 _boundsMax{0.0f};
	};
}