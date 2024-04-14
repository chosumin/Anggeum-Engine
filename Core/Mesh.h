#pragma once
#include "Component.h"

struct Vertex;

namespace Core
{
	class Buffer;
	class Material;
	class CommandBuffer;

	class Mesh
	{
	public:
		Mesh(Device& device, string modelPath);
		~Mesh();

		uint32_t GetIndexCount() const 
		{
			return static_cast<uint32_t>(_indices.size());
		}

		vector<Buffer*> GetVertexBuffers(vector<string> names) const;
		Buffer& GetIndexBuffer() { return *_indexBuffer; }

		string GetModelPath() const { return _modelPath; }
	private:
		void LoadModel(const string& modelPath);
		void CreateVertexBuffer();
		void CreateIndexBuffer();
	private:
		string _modelPath;

		Device& _device;

		unordered_map<string, vector<uint8_t>> _vertices;
		vector<uint32_t> _indices;

		unordered_map<string, Buffer*> _vertexBuffers;

		Buffer* _indexBuffer;
	};
}
