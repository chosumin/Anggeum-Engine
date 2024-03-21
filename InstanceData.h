#pragma once

namespace Core
{
	struct InstanceData
	{
		mat4 World;
	};

	class Buffer;
	class InstanceBuffer
	{
	public:
		InstanceBuffer(Device& device);
		virtual ~InstanceBuffer();

		Buffer& GetBuffer() { return *_instanceBuffer; }

		void SetBuffer(uint32_t index, mat4 world);
		void Copy();
	private:
		Buffer* _instanceBuffer;
		void* _instanceDataMapped;
		vector<InstanceData> _instanceData;
		const uint32_t _maxInstanceCount = 500;
		VkDeviceSize _bufferSize;
	};
}

