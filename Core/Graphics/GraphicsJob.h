#pragma once
#include "Foundation/Job.h"

namespace Core
{
	class CommandBuffer;
	class Buffer;
	class VkBufferJob : public Job
	{
	public:
		VkBufferJob(Device& device, VkBufferUsageFlagBits usageFlag, Buffer** dstBuffer, vector<uint8_t> vertexData);
		~VkBufferJob();

		void Execute(CommandBuffer& commandBuffer) override;
	private:
		Device& _device;
		vector<uint8_t> _vertexData;

		VkBufferUsageFlagBits _usageFlag;

		Buffer** _destination;
		Buffer* _stagingBuffer;
	};

	class Image;
	class VkImageJob : public Job
	{
	public:
		VkImageJob(Device& device, Image& dstImage, string filePath);
		~VkImageJob();

		void Execute(CommandBuffer& commandBuffer) override;
	private:
		Device& _device;
		string _filePath;

		Image& _dstImage;
		Buffer* _stagingBuffer;
	};
}

