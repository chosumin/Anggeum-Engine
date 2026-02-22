#pragma once
#include "DescriptorPool.h"

namespace Core
{
	class Shader;

	/*
	 * DescriptorSetBuilder accumulates binding entries from multiple sources
	 * (Pass + RendererBatch) and builds a DescriptorSetResources with
	 * allocated VkDescriptorSet + created buffers.
	 *
	 * Usage:
	 *   auto& builder = renderFrame.CreateDescriptorSetBuilder(shader);
	 *   builder.SetUniformBuffer(0, &cameraData);    // Pass fills its bindings
	 *   // ... hand off to RendererBatch ...
	 *   builder.SetStorageBuffer(1, transformBuffer); // Batch fills its bindings
	 *   builder.Build();                              // Allocate + Update
	 *   commandBuffer.BindDescriptorSet(..., builder.GetResources());
	 */
	class DescriptorSetBuilder
	{
	public:
		DescriptorSetBuilder(Device& device, DescriptorPool& pool, 
			Shader& shader, uint32_t setIndex = 0);

		// --- Uniform buffer: creates internal UBO from layout size, copies data ---
		DescriptorSetBuilder& SetUniformBuffer(uint32_t binding, void* data);

		// --- Uniform buffer: uses an existing UBO (no copy, caller owns lifetime) ---
		DescriptorSetBuilder& SetUniformBuffer(uint32_t binding, UniformBuffer* buffer);

		// --- Storage buffer: wraps an existing buffer ---
		DescriptorSetBuilder& SetStorageBuffer(uint32_t binding, Buffer* buffer);

		// --- Texture: combined image sampler ---
		DescriptorSetBuilder& SetTextureBuffer(uint32_t binding, 
			shared_ptr<Texture> texture,
			uint32_t mipLevel = 0,
			VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// --- Build: allocate descriptor set and update all bindings ---
		DescriptorSetResources& Build();

		// --- Accessors ---
		DescriptorSetResources& GetResources() { return _resources; }
		Shader& GetShader() { return _shader; }
		uint32_t GetSetIndex() const { return _setIndex; }

	private:
		Device& _device;
		DescriptorPool& _pool;
		Shader& _shader;
		uint32_t _setIndex;

		DescriptorSetResources _resources;
	};
}