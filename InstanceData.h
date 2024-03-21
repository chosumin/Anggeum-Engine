#pragma once

namespace Core
{
	struct InstanceData
	{
		mat4 World;
	};

	class Buffer;
	class Transform;
	class InstanceBuffer
	{
	public:
		InstanceBuffer(Device& device);
		virtual ~InstanceBuffer();

		Buffer& GetBuffer() { return *_instanceBuffer; }

		void SetBuffer(size_t index, Transform& transform);
		void Copy();
	private:
		Buffer* _instanceBuffer;
		void* _instanceDataMapped;
		vector<InstanceData> _instanceData;
		const uint32_t _maxInstanceCount = 500;
		VkDeviceSize _bufferSize;
	};
}

