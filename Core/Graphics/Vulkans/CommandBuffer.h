#pragma once
#include "CommandPool.h"


namespace Core
{
	class Device;
	class RenderPass;
	class Pipeline;
	class Material;
	class RenderFrame;
	class Buffer;
	class Shader;
	class Image;
	class Job;
	class Framebuffer;

	class CommandBuffer
	{
	public:
		CommandBuffer(Device& device, CommandPool& commandPool, VkCommandBufferLevel level);
		~CommandBuffer() = default;

		const VkCommandBuffer& GetHandle() const { return _commandBuffer; }

		void ResetCommandBuffer();
		void BeginCommandBuffer(VkCommandBufferUsageFlags flags, 
			const RenderPass* renderPass, const Framebuffer* framebuffer, 
			uint32_t subpassIndex, uint32_t imageIndex);
		void BeginCommandBuffer(bool isSingleTime = false);
		void ExecuteCommands(vector<CommandBuffer*>& secondaryCommandBuffers);
		void BeginRenderPass(VkRenderPassBeginInfo renderPassInfo);
		void BindPipeline(const Pipeline* pipeline);
		void SetViewportAndScissor(VkExtent2D extent);
		void BindDescriptorSets(RenderFrame& renderFrame, VkPipelineBindPoint pipelineBindPoint, Material& material);
		void BindDescriptorSets(RenderFrame& renderFrame, VkPipelineBindPoint pipelineBindPoint, Shader& shader);

		void PushConstants(Material& material, uint32_t index = 0);
		void BindVertexBuffers(Buffer& buffer, uint32_t binding);
		void BindVertexBuffers(vector<Buffer*> buffers, uint32_t binding);
		void BindIndexBuffer(Buffer& buffer, VkIndexType indexType);
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstInstance = 0);
		void Draw(uint32_t vertexCount, uint32_t instanceCount);
		void Dispatch(uint32_t x, uint32_t y, uint32_t z);
		void CopyBuffer(Buffer& srcBuffer, Buffer& dstBuffer);
		void CopyImage(Image& srcImage, Image& dstImage, 
			uint32_t srcMipLevel, uint32_t srcLayer, 
			uint32_t dstMipLevel, uint32_t dstLayer);
		void CopyBufferToImage(Buffer& buffer, Image& image, uint32_t width, uint32_t height);
		void TransitionImageLayout(Image& image, 
			VkImageLayout oldLayout, VkImageLayout newLayout);
		void GenerateMipmaps(Image& image, uint32_t mipLevels);
		void EndRenderPass();
		void EndCommandBuffer();

		void UpdateFrame(uint64_t frame) { _frame = frame; }
		bool IsBusy();

		static void ImmediateSubmit(Device& device, Job& job);
		static void ImmediateSubmit(Device& device, std::vector<Job*>& jobs);
	private:
		void BindBindlessDescriptorSet(
			RenderFrame& renderFrame,
			VkPipelineBindPoint pipelineBindPoint,
			VkPipelineLayout pipelineLayout);

		void GetAccessAndStageFlags(const VkImageLayout& inImageLayout, VkAccessFlags& outAccessFlags, VkPipelineStageFlags& outPipelineStageFlags);
	private:
		Device& _device;
		VkCommandBuffer _commandBuffer;
		VkCommandBufferLevel _level;

		//hack : have to be managed in resource system or something
		uint64_t _frame;

		bool _bindlessDescriptorSetBound = false;
	};
}

