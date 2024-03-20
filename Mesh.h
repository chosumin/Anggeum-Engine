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

		Buffer& GetVertexBuffer() { return *_vertexBuffer; }
		Buffer& GetIndexBuffer() { return *_indexBuffer; }

		string GetModelPath() const { return _modelPath; }
	private:
		void LoadModel(const string& modelPath);
		void CreateVertexBuffer();
		void CreateIndexBuffer();
	private:
		string _modelPath;

		Device& _device;

		//clockwise.
		vector<Vertex> _vertices;
		vector<uint32_t> _indices;

		Buffer* _vertexBuffer;
		Buffer* _indexBuffer;
	};
}
