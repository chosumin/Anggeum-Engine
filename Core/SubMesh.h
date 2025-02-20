#pragma once

namespace Core
{
	class Buffer;
	class SubMesh
	{
	public:
		SubMesh(Device& device, string name);
		~SubMesh();

		uint32_t GetIndexCount() const { return _indexCount; }
		void SetIndexCount(uint32_t count) { _indexCount = count; }

		vector<Buffer*> GetVertexBuffers(vector<string> names) const;
		Buffer& GetIndexBuffer() { return *_indexBuffer; }

		string GetName() const { return _name; }

		void CreateVertexBuffer(string name, vector<uint8_t>& vertexData);
		void CreateIndexBuffer(vector<uint8_t>& vertexData);
	private:
		Device& _device;

		string _name;

		uint32_t _indexCount;

		//Key: Attribute name, Value: Attribute value
		unordered_map<string, Buffer*> _vertexBuffers;
		Buffer* _indexBuffer;
	};
}