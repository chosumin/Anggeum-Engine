#pragma once

namespace Core
{
	class Pipeline;
	class SwapChain;
	class Shader;
	class Buffer;
	class Material;
	class CommandPool;
	class Device;
	class CommandBuffer
	{
	public:
		CommandBuffer(Device& device, CommandPool& commandPool);
		~CommandBuffer() = default;

		VkCommandBuffer& GetHandle() { return _commandBuffer; }

		void ResetCommandBuffer();
		void BeginCommandBuffer();
		void BeginRenderPass(VkRenderPassBeginInfo renderPassInfo);
		void BindPipeline(VkPipelineBindPoint pipelineBindPoint,
			VkPipeline pipeline);
		void SetViewportAndScissor(VkViewport viewport, VkRect2D scissor);
		void BindDescriptorSets(VkPipelineBindPoint pipelineBindPoint, Shader& shader, uint32_t currentFrame);
		void Flush(Material& material);
		void BindVertexBuffers(Buffer& buffer);
		void BindIndexBuffer(Buffer& buffer, VkIndexType indexType);
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount);
		void EndCommandBuffer();
	private:
		Device& _device;
		VkCommandBuffer _commandBuffer;
	};
}

