#include "stdafx.h"
#include "SDFGenerator.h"
#include "Vulkans/Device.h"
#include "Vulkans/Image.h"
#include "Vulkans/Texture.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Components/Mesh.h"
#include "Foundation/Entity.h"
#include "SubMesh.h"
#include "ResourceCache.h"
#include "TransferJob.h"

using namespace Core;

SDFGenerator::SDFGenerator(Device& device)
	: _device(device)
{
}

void SDFGenerator::ComputeSceneBounds(const std::vector<Mesh*>& meshes)
{
	_boundsMin = glm::vec3(FLT_MAX);
	_boundsMax = glm::vec3(-FLT_MAX);

	for (auto* mesh : meshes)
	{
		auto& transform = mesh->GetEntity().GetTransform();
		glm::mat4 model = transform.GetMatrix();

		for (auto& subMesh : mesh->GetSubMeshes())
		{
			auto& vertices = subMesh->GetPositions();
			for (auto& pos : vertices)
			{
				glm::vec3 worldPos = glm::vec3(model * glm::vec4(pos, 1.0f));
				_boundsMin = glm::min(_boundsMin, worldPos);
				_boundsMax = glm::max(_boundsMax, worldPos);
			}
		}
	}

	// Add padding to prevent edge artifacts
	glm::vec3 padding = (_boundsMax - _boundsMin) * 0.1f;
	_boundsMin -= padding;
	_boundsMax += padding;
}

float SDFGenerator::DistanceToMeshes(const glm::vec3& point,
	const std::vector<Mesh*>& meshes) const
{
	float minDist = FLT_MAX;

	for (auto* mesh : meshes)
	{
		auto& transform = mesh->GetEntity().GetTransform();
		glm::mat4 model = transform.GetMatrix();

		for (auto& subMesh : mesh->GetSubMeshes())
		{
			auto& positions = subMesh->GetPositions();
			auto& indices = subMesh->GetIndices();

			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				glm::vec3 v0 = glm::vec3(model * glm::vec4(positions[indices[i + 0]], 1.0f));
				glm::vec3 v1 = glm::vec3(model * glm::vec4(positions[indices[i + 1]], 1.0f));
				glm::vec3 v2 = glm::vec3(model * glm::vec4(positions[indices[i + 2]], 1.0f));

				// Point-to-triangle distance
				glm::vec3 edge0 = v1 - v0;
				glm::vec3 edge1 = v2 - v0;
				glm::vec3 v0p = v0 - point;

				float a = glm::dot(edge0, edge0);
				float b = glm::dot(edge0, edge1);
				float c = glm::dot(edge1, edge1);
				float d = glm::dot(edge0, v0p);
				float e = glm::dot(edge1, v0p);

				float det = a * c - b * b;
				float s = b * e - c * d;
				float t = b * d - a * e;

				if (s + t <= det)
				{
					if (s < 0.0f)
					{
						if (t < 0.0f)
						{
							// Region 4
							if (d < 0.0f) { s = glm::clamp(-d / a, 0.0f, 1.0f); t = 0.0f; }
							else { s = 0.0f; t = glm::clamp(-e / c, 0.0f, 1.0f); }
						}
						else
						{
							// Region 3
							s = 0.0f;
							t = glm::clamp(-e / c, 0.0f, 1.0f);
						}
					}
					else if (t < 0.0f)
					{
						// Region 5
						s = glm::clamp(-d / a, 0.0f, 1.0f);
						t = 0.0f;
					}
					else
					{
						// Region 0
						float invDet = 1.0f / det;
						s *= invDet;
						t *= invDet;
					}
				}
				else
				{
					if (s < 0.0f)
					{
						// Region 2
						float tmp0 = b + d;
						float tmp1 = c + e;
						if (tmp1 > tmp0) {
							float numer = tmp1 - tmp0;
							float denom = a - 2.0f * b + c;
							s = glm::clamp(numer / denom, 0.0f, 1.0f);
							t = 1.0f - s;
						} else {
							s = 0.0f;
							t = glm::clamp(-e / c, 0.0f, 1.0f);
						}
					}
					else if (t < 0.0f)
					{
						// Region 6
						float tmp0 = b + e;
						float tmp1 = a + d;
						if (tmp1 > tmp0) {
							float numer = tmp1 - tmp0;
							float denom = a - 2.0f * b + c;
							t = glm::clamp(numer / denom, 0.0f, 1.0f);
							s = 1.0f - t;
						} else {
							s = glm::clamp(-d / a, 0.0f, 1.0f);
							t = 0.0f;
						}
					}
					else
					{
						// Region 1
						float numer = (c + e) - (b + d);
						if (numer <= 0.0f) {
							s = 0.0f;
						} else {
							float denom = a - 2.0f * b + c;
							s = glm::clamp(numer / denom, 0.0f, 1.0f);
						}
						t = 1.0f - s;
					}
				}

				glm::vec3 closest = v0 + s * edge0 + t * edge1;
				float dist = glm::length(point - closest);
				minDist = glm::min(minDist, dist);
			}
		}
	}

	return minDist;
}

shared_ptr<Texture> SDFGenerator::Generate(const std::vector<Mesh*>& meshes,
	uint32_t resolution)
{
	ComputeSceneBounds(meshes);

	std::vector<float> sdfData(resolution * resolution * resolution);
	glm::vec3 volumeSize = _boundsMax - _boundsMin;
	glm::vec3 voxelSize = volumeSize / static_cast<float>(resolution);

	std::cout << "Generating SDF volume (" << resolution << "^3)..." << std::endl;

	// Parallelise with OpenMP if available, otherwise sequential
	#pragma omp parallel for schedule(dynamic)
	for (int z = 0; z < static_cast<int>(resolution); ++z)
	{
		for (uint32_t y = 0; y < resolution; ++y)
		{
			for (uint32_t x = 0; x < resolution; ++x)
			{
				glm::vec3 point = _boundsMin + glm::vec3(
					(x + 0.5f) * voxelSize.x,
					(y + 0.5f) * voxelSize.y,
					(z + 0.5f) * voxelSize.z);

				float dist = DistanceToMeshes(point, meshes);
				size_t index = z * resolution * resolution + y * resolution + x;
				sdfData[index] = dist;
			}
		}

		if (z % 16 == 0)
		{
			std::cout << "  SDF progress: " << z << "/" << resolution << std::endl;
		}
	}

	std::cout << "SDF generation complete. Uploading to GPU..." << std::endl;

	return Upload3DTexture(sdfData, resolution);
}

shared_ptr<Texture> SDFGenerator::Upload3DTexture(const std::vector<float>& data,
	uint32_t resolution)
{
	VkDeviceSize imageSize = data.size() * sizeof(float);

	// Staging buffer
	Buffer stagingBuffer(_device, imageSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		MemoryType::HOST_VISIBLE);
	stagingBuffer.Update(data.data(), imageSize);

	// Create 3D image
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_3D;
	imageInfo.extent = { resolution, resolution, resolution };
	imageInfo.format = VK_FORMAT_R32_SFLOAT;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	auto image = make_shared<Image>(_device, imageInfo,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_3D);

	// Transfer: transition ¡æ copy ¡æ transition
	auto transferCmd = [&](CommandBuffer& cmd)
	{
		cmd.TransitionImageLayout(*image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { resolution, resolution, resolution };

		vkCmdCopyBufferToImage(cmd.GetHandle(),
			stagingBuffer.GetBuffer(),
			image->GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region);

		cmd.TransitionImageLayout(*image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	};

	// Immediate submit
	auto& commandPool = _device.GetCommandPool();
	CommandBuffer cmd(_device, commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	cmd.BeginCommandBuffer(true);
	transferCmd(cmd);
	cmd.EndCommandBuffer();

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	auto handle = cmd.GetHandle();
	submitInfo.pCommandBuffers = &handle;

	vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(_device.GetGraphicsQueue());

	auto sampler = _device.GetResourceCache().RequestSampler(DEFAULT_SAMPLER);
	return make_shared<Texture>("SDFVolume", image, sampler);
}