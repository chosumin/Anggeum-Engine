#pragma once
#include "AsyncLoadable.h"

namespace Core
{
	class Buffer;
	class CommandBuffer;
	class SubMesh : public AsyncLoadable
	{
	public:
		SubMesh(Device& device, string name);
		~SubMesh();

		uint32_t GetIndexCount() const { return _indexCount; }
		void SetIndexCount(uint32_t count) { _indexCount = count; }

		vector<Buffer*> GetVertexBuffers(vector<string> names) const;
		Buffer& GetIndexBuffer() { return *_indexBuffer; }

		string GetName() const { return _name; }

		VkIndexType GetIndexType() const { return _indexType; }

		bool HasVertexAttribute(string attributeName) const;

		void CreateVertexBuffer(string name, vector<uint8_t>& vertexData);
		void CreateIndexBuffer(vector<uint8_t>& vertexData, VkIndexType indexType);

		virtual void Load(CommandBuffer& commandBuffer) override;
	private:
		Device& _device;

		string _name;

		uint32_t _indexCount;
		VkIndexType _indexType;

		//Key: Attribute name, Value: Attribute value
		unordered_map<string, Buffer*> _vertexBuffers;
		unordered_map<string, Buffer*> _vertexStagingBuffers;

		Buffer* _indexBuffer;
		Buffer* _indexStagingBuffer;
	};
}