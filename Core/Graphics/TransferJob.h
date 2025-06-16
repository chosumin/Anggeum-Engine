#pragma once
#include "Foundation/Job.h"

namespace Core
{
	class CommandBuffer;
	class Buffer;
	class VkBufferJob : public Job
	{
	public:
		VkBufferJob(Device& device, VkBufferUsageFlags usageFlag, Buffer** dstBuffer, vector<uint8_t> vertexData, bool isStorageBuffer = false);
		~VkBufferJob();

		void Execute() override;
	private:
		Device& _device;
		vector<uint8_t> _vertexData;

		VkBufferUsageFlags _usageFlag;

		Buffer** _destination;
		Buffer* _stagingBuffer;
	};

	class Image;
	class VkImageJob : public Job
	{
	public:
		VkImageJob(Device& device, weak_ptr<Image> dstImage, string filePath);
		~VkImageJob();

		void Execute() override;
	private:
		Device& _device;
		string _filePath;

		weak_ptr<Image> _dstImage;
		Buffer* _stagingBuffer;
	};
}

