#include "stdafx.h"
#include "Mesh.h"
#include "Vertex.h"
#include "CommandBuffer.h"
#include "Buffer.h"
#include "Transform.h"
#include "Entity.h"
#include "Material.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Core::Mesh::Mesh(Device& device, Entity& entity, string modelPath)
	:Component(entity), _device(device)
{
	LoadModel(modelPath);

	CreateVertexBuffer();
	CreateIndexBuffer();
}

Core::Mesh::~Mesh()
{
	delete(_indexBuffer);
	delete(_vertexBuffer);
}

void Core::Mesh::UpdateFrame(float deltaTime)
{
}

void Core::Mesh::DrawFrame(CommandBuffer& commandBuffer, Material& material)
{
	auto& transform = _entity.GetComponent<Transform>();
	_modelPushConstant.World = transform.GetMatrix();

	material.SetPushConstants(_modelPushConstant);
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

			if (uniqueVertices.count(vertex) == 0)
			{
				uniqueVertices[vertex] = static_cast<uint32_t>(_vertices.size());
				_vertices.push_back(vertex);
			}

			_indices.push_back(uniqueVertices[vertex]);
		}
	}
}

void Core::Mesh::CreateVertexBuffer()
{
	VkDeviceSize bufferSize = sizeof(_vertices[0]) * _vertices.size();

	Core::Buffer stagingBuffer(_device, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(_vertices.data(), bufferSize);

	//todo : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT to use compute shader.
	_vertexBuffer = new Core::Buffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//vertex memory is moved from CPU to GPU
	_vertexBuffer->CopyBuffer(stagingBuffer.GetBuffer(), bufferSize);
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

std::type_index Core::Mesh::GetType()
{
	return typeid(Mesh);
}

void Core::Mesh::Resize(uint32_t width, uint32_t height)
{
}
