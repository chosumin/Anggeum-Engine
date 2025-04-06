#pragma once

namespace Core
{
	class Pipeline;
	class SwapChain;
	class Buffer;
	class Material;
	class CommandPool;
	class Device;
	class Image;
	class CommandBuffer
	{
	public:
		CommandBuffer(Device& device, CommandPool& commandPool);
		~CommandBuffer() = default;

		const VkCommandBuffer& GetHandle() const { return _commandBuffer; }

		void ResetCommandBuffer();
		void BeginCommandBuffer(bool isSingleTime = false);
		void BeginRenderPass(VkRenderPassBeginInfo renderPassInfo);
		void BindPipeline(const Pipeline* pipeline);
		void SetViewportAndScissor(VkExtent2D extent);
		void BindDescriptorSets(VkPipelineBindPoint pipelineBindPoint, Material& material, uint32_t currentFrame);
		void PushConstants(Material& material);
		void BindVertexBuffers(Buffer& buffer, uint32_t binding);
		void BindVertexBuffers(vector<Buffer*> buffers, uint32_t binding);
		void BindIndexBuffer(Buffer& buffer, VkIndexType indexType);
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount);
		void CopyImage(Image& srcImage, Image& dstImage, uint32_t srcMipLevel, uint32_t srcLayer, uint32_t dstMipLevel, uint32_t dstLayer);
		void TransitionImageLayout(Image& image, 
			VkImageLayout oldLayout, VkImageLayout newLayout);
		void EndRenderPass();
		void EndCommandBuffer();
	private:
		Device& _device;
		VkCommandBuffer _commandBuffer;
	};
}

