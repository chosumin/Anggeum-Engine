#pragma once

namespace Core
{
	class Pipeline;
	class SwapChain;
	class Shader;
	class Buffer;
	class Material;
	class CommandBuffer
	{
	public:
		CommandBuffer(VkCommandBuffer commandBuffer);
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
		VkCommandBuffer _commandBuffer;
	};
}

