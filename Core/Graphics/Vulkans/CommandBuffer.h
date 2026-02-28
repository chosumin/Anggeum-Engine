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
	class MeshBufferManager;
	struct DescriptorSetResources;

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
		void BindDescriptorSetsWithKey(
			RenderFrame& renderFrame,
			VkPipelineBindPoint pipelineBindPoint,
			Shader& shader,
			size_t key);
		void BindDescriptorSet(
			RenderFrame& renderFrame,
			VkPipelineBindPoint pipelineBindPoint,
			Shader& shader,
			uint32_t setIndex,
			DescriptorSetResources& resources);
		void PushConstants(Material& material, uint32_t index = 0);
		void PushConstants(Shader& shader, uint index, const void* data);

		void BindVertexBuffers(Buffer& buffer, uint32_t binding);
		void BindVertexBuffers(vector<Buffer*> buffers, uint32_t binding);
		void BindIndexBuffer(Buffer& buffer, VkIndexType indexType);
		
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstInstance = 0);
		void Draw(uint32_t vertexCount, uint32_t instanceCount);
		void DrawIndexedIndirect(Buffer& indirectBuffer, uint32_t drawCount, uint32_t stride);
		void Dispatch(uint32_t x, uint32_t y, uint32_t z);

		void FillBuffer(Buffer& buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data);

		void Barrier(
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			VkAccessFlags srcAccessMask,
			VkAccessFlags dstAccessMask);

		void BufferBarrier(
			Buffer& buffer,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			VkAccessFlags srcAccessMask,
			VkAccessFlags dstAccessMask);

		void CopyBuffer(Buffer& srcBuffer, Buffer& dstBuffer, VkDeviceSize dstOffset);
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

		// Debug marker functions
		void BeginDebugMarker(const char* markerName, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
		void EndDebugMarker();
		void InsertDebugMarker(const char* markerName, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);

		void SetDepthBias(float constantFactor, float clamp, float slopeFactor)
		{
			vkCmdSetDepthBias(_commandBuffer, constantFactor, clamp, slopeFactor);
		}
	private:
		void BindBindlessDescriptorSet(
			RenderFrame& renderFrame,
			VkPipelineBindPoint pipelineBindPoint,
			VkPipelineLayout pipelineLayout);

		void GetAccessAndStageMask(const VkImageLayout& inImageLayout, VkAccessFlags& outAccessFlags, VkPipelineStageFlags& outPipelineStageFlags);
	private:
		Device& _device;
		VkCommandBuffer _commandBuffer;
		VkCommandBufferLevel _level;

		//hack : have to be managed in resource system or something
		uint64_t _frame;

		bool _bindlessDescriptorSetBound = false;
	};
}
