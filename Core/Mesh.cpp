#include "stdafx.h"
#include "Mesh.h"
#include "Vertex.h"
#include "CommandBuffer.h"
#include "Buffer.h"
#include "Utility.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

void AddVertex(unordered_map<string, vector<uint8_t>>& vertices, Vertex vertex)
{
	//Separate vertex attributes.
	{
		auto bytes = Core::Utility::ToBytes(vertex.Pos);
		vertices[VertexAttributeName::Position].insert(vertices[VertexAttributeName::Position].end(), bytes.begin(), bytes.end());
	}

	{
		auto bytes = Core::Utility::ToBytes(vertex.TexCoord);
		vertices[VertexAttributeName::UV].insert(vertices[VertexAttributeName::UV].end(), bytes.begin(), bytes.end());
	}

	{
		auto bytes = Core::Utility::ToBytes(vertex.Color);
		vertices[VertexAttributeName::Col].insert(vertices[VertexAttributeName::Col].end(), bytes.begin(), bytes.end());
	}
}

Core::Mesh::Mesh(Device& device, string modelPath)
	:_device(device), _modelPath(modelPath)
{
	LoadModel(modelPath);

	CreateVertexBuffer();
	CreateIndexBuffer();
}

Core::Mesh::Mesh(Device& device, int polygonType)
	:_device(device)
{
	LoadPlane();

	CreateVertexBuffer();
	CreateIndexBuffer();
}

Core::Mesh::~Mesh()
{
	delete(_indexBuffer);

	for (auto&& vertexBuffer : _vertexBuffers)
	{
		delete(vertexBuffer.second);
	}
	_vertexBuffers.clear();
}

vector<Core::Buffer*> Core::Mesh::GetVertexBuffers(vector<string> names) const
{
	vector<Core::Buffer*> buffers;

	for (string name : names)
	{
		auto vertexBuffer = _vertexBuffers.find(name);
		assert(vertexBuffer != _vertexBuffers.end(),
			"Invalid vertex attribute name!");
		
		buffers.push_back(vertexBuffer->second);
	}

	return buffers;
}

void Core::Mesh::LoadModel(const string& modelPath)
{
	tinyobj::attrib_t attrib;
	vector<tinyobj::shape_t> shapes;
	vector<tinyobj::material_t> materials;
	string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str()))
		throw runtime_error(warn + err);

	unordered_map<Vertex, uint32_t> uniqueVertices;

	for (const auto& shape : shapes)
	{
		for (const auto& index : shape.mesh.indices)
		{
			Vertex vertex{};

			vertex.Pos =
			{
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};

			vertex.TexCoord =
			{
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
			};

			vertex.Color = { 1.0f, 1.0f, 1.0f };

			//Create new vertex.
			if (uniqueVertices.count(vertex) == 0)
			{
				auto vertices = _vertices.begin();
				if (vertices != _vertices.end())
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices->second.size() / sizeof(vec3));
				else
					uniqueVertices[vertex] = 0;

				AddVertex(_vertices, vertex);
			}

			_indices.push_back(uniqueVertices[vertex]);
		}
	}
}

void Core::Mesh::LoadPlane()
{
	Vertex vertices[4];

	vertices[0].Pos = { -2,0,-2 };
	vertices[0].TexCoord = { 0, 1 };
	vertices[0].Color = { 1.0f, 1.0f, 1.0f };

	vertices[1].Pos = { -2,0,2 };
	vertices[1].TexCoord = { 0, 0 };
	vertices[1].Color = { 1.0f, 1.0f, 1.0f };

	vertices[2].Pos = { 2,0,-2};
	vertices[2].TexCoord = { 1, 1 };
	vertices[2].Color = { 1.0f, 1.0f, 1.0f };

	vertices[3].Pos = { 2,0,2 };
	vertices[3].TexCoord = { 1, 0 };
	vertices[3].Color = { 1.0f, 1.0f, 1.0f };

	for (auto&& vertex : vertices)
	{
		AddVertex(_vertices, vertex);
	}

	_indices = { 0,1,2,2,1,3 };
}

void Core::Mesh::CreateVertexBuffer()
{
	for (auto& vertices : _vertices)
	{
		vector<uint8_t> verticesValue = vertices.second;

		VkDeviceSize bufferSize = sizeof(verticesValue[0]) * verticesValue.size();

		Core::Buffer stagingBuffer(_device, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		stagingBuffer.CopyBuffer(verticesValue.data(), bufferSize);

		//todo : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT to use compute shader.
		auto vertexBuffer = new Core::Buffer(_device, bufferSize,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		//vertex memory is moved from CPU to GPU
		vertexBuffer->CopyBuffer(stagingBuffer.GetBuffer(), bufferSize);

		_vertexBuffers[vertices.first] = vertexBuffer;
	}
}

void Core::Mesh::CreateIndexBuffer()
{
	VkDeviceSize bufferSize = sizeof(_indices[0]) * _indices.size();

	Core::Buffer stagingBuffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(_indices.data(), bufferSize);

	_indexBuffer = new Core::Buffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	_indexBuffer->CopyBuffer(stagingBuffer.GetBuffer(), bufferSize);
}