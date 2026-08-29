#pragma once
#include "CommandPool.h"
#include "BarrierBatch.h"
#include "RenderingSetup.h"
#include "Graphics/SyncContext.h"

namespace Core
{
	class Device;
	class Pipeline;
	class RenderFrame;
	class Buffer;
	class Shader;
	class Image;
	class Texture;
	class Job;
	class MeshBufferManager;
	struct DescriptorSetResources;

	class CommandBuffer
	{
	public:
		CommandBuffer(Device& device, CommandPool& commandPool, VkCommandBufferLevel level);
		~CommandBuffer() = default;

		const VkCommandBuffer& GetHandle() const { return _commandBuffer; }

		void ResetCommandBuffer();
		void BeginCommandBuffer(VkCommandBufferUsageFlags flags = 0);
		void ExecuteCommands(vector<CommandBuffer*>& secondaryCommandBuffers);
		void EndCommandBuffer();

		void BeginRendering(const RenderingSetup& setup);
		void EndRendering();
		
		void BindPipeline(const Pipeline* pipeline);
		
		void SetViewportAndScissor(VkExtent2D extent);
		
		void BindDescriptorSets(VkPipelineBindPoint pipelineBindPoint,
			Shader& shader,
			const vector<DescriptorSetResources*>& resourcesList);
		void BindDescriptorSet(VkPipelineBindPoint pipelineBindPoint,
			Shader& shader,
			DescriptorSetResources& resources);

		// Records the push constant range at `index` of the shader. `value` must be
		// at least as large as the range the shader declares; asserted at record time.
		template <typename T>
		void PushConstants(Shader& shader, uint32_t index, const T& value)
		{
			static_assert(!std::is_pointer_v<T>,
				"Pass the push constant value itself, not a pointer to it.");

			PushConstantsInternal(shader, index, &value, sizeof(T));
		}

		void BindVertexBuffers(Buffer& buffer, uint32_t binding);
		void BindVertexBuffers(vector<Buffer*> buffers, uint32_t binding);
		void BindIndexBuffer(Buffer& buffer, VkIndexType indexType);
		
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstInstance = 0);
		void Draw(uint32_t vertexCount, uint32_t instanceCount);
		void DrawIndexedIndirect(Buffer& indirectBuffer, uint32_t drawCount, uint32_t stride);
		void DrawIndexedIndirectCount(Buffer& indirectBuffer, VkDeviceSize indirectOffset,
			Buffer& countBuffer, VkDeviceSize countOffset,
			uint32_t maxDrawCount, uint32_t stride);
		void Dispatch(uint32_t x, uint32_t y, uint32_t z);
		void DispatchIndirect(Buffer& argsBuffer, VkDeviceSize offset = 0);

		void FillBuffer(Buffer& buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data);

		BarrierBatch CreateBarrierBatch();

		void CopyBuffer(Buffer& srcBuffer, Buffer& dstBuffer,
			VkDeviceSize dstOffset = 0, VkDeviceSize srcOffset = 0, VkDeviceSize size = 0);

		void CopyImage(Texture& srcTexture, Texture& dstTexture,
			uint32_t srcMipLevel, uint32_t srcLayer,
			uint32_t dstMipLevel, uint32_t dstLayer);
		void CopyBufferToImage(Buffer& buffer, Texture& texture, uint32_t width, uint32_t height,
			VkDeviceSize bufferOffset = 0);
		void CopyBufferToImage(Buffer& buffer, Texture& texture,
			const vector<VkBufferImageCopy>& regions);
		// Mip 0 -> buffer offset 0; `layout` is the image's current layout.
		void CopyImageToBuffer(Texture& texture, VkImageLayout layout, Buffer& buffer,
			uint32_t width, uint32_t height);

		void GenerateMipmaps(Texture& texture, uint32_t mipLevels);

		// Recycling contract: busy from checkout until the stamped timeline
		// value completes. 
		void MarkCheckedOut() { _submitValue = UINT64_MAX; }
		void MarkSubmitted(uint64_t timelineValue) { _submitValue = timelineValue; }
		bool IsBusy(uint64_t timelineCompleted) const
		{
			uint64_t value = _submitValue.load();
			return value == UINT64_MAX || timelineCompleted < value;
		}

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
		void PushConstantsInternal(Shader& shader, uint32_t index, const void* data, uint32_t size);
	private:
		Device& _device;
		VkCommandBuffer _commandBuffer;
		VkCommandBufferLevel _level;

		// Queue family this command buffer is recorded for. Used to strip
		// pipeline stages that the queue does not support (e.g. fragment shader
		// stage on a dedicated compute queue).
		uint32_t _queueFamilyIndex;

		// Timeline stamp; atomic because workers check out on their thread
		// while the main thread stamps at submit. 0 = never used, recyclable.
		std::atomic<uint64_t> _submitValue{ 0 };
	};
}
