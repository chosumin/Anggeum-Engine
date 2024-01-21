#include "stdafx.h"
#include "Polygon.h"
#include "Vertex.h"
#include "CommandBuffer.h"
#include "Buffer.h"
#include "Transform.h"
#include "MVPUniformBuffer.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Core::Polygon::Polygon(VkCommandPool commandPool, vec3 position, string modelPath, ModelUniformBuffer* buffer)
{
	_buffer = buffer;

	_transform = make_unique<Transform>();
	_transform->SetTranslation(position);

	LoadModel(modelPath);

	CreateVertexBuffer(commandPool);
	CreateIndexBuffer(commandPool);
}

Core::Polygon::~Polygon()
{
	delete(_indexBuffer);
	delete(_vertexBuffer);
}

void Core::Polygon::DrawFrame(VkCommandBuffer commandBuffer, uint32_t index) const
{
	_buffer->BufferObject = _transform->GetMatrix();
	_buffer->Update(index);

	VkBuffer vertexBuffers[] = { _vertexBuffer->GetBuffer() };
	VkDeviceSize offsets[] = { 0 };

	//hack : optimizable with https://developer.nvidia.com/vulkan-memory-management
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

	vkCmdBindIndexBuffer(commandBuffer, _indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
}

void Core::Polygon::LoadModel(const string& modelPath)
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

void Core::Polygon::CreateVertexBuffer(VkCommandPool commandPool)
{
	VkDeviceSize bufferSize = sizeof(_vertices[0]) * _vertices.size();

	Core::Buffer stagingBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(_vertices.data(), bufferSize);

	//todo : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT to use compute shader.
	_vertexBuffer = new Core::Buffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//vertex memory is moved from CPU to GPU
	_vertexBuffer->CopyBuffer(commandPool, stagingBuffer.GetBuffer(), bufferSize);
}

void Core::Polygon::CreateIndexBuffer(VkCommandPool commandPool)
{
	VkDeviceSize bufferSize = sizeof(_indices[0]) * _indices.size();

	Core::Buffer stagingBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(_indices.data(), bufferSize);

	_indexBuffer = new Core::Buffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	_indexBuffer->CopyBuffer(commandPool, stagingBuffer.GetBuffer(), bufferSize);
}