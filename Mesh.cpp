#include "stdafx.h"
#include "Mesh.h"
#include "Vertex.h"
#include "CommandBuffer.h"
#include "Buffer.h"
#include "Transform.h"
#include "InputEvents.h"
#include "Texture.h"
#include "IDescriptor.h"
#include "Entity.h"
#include "Pipeline.h"
#include "IPushConstant.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Core::ModelPushConstant::ModelPushConstant()
	:Buffer(), _offset(0)
{
}

uint32_t Core::ModelPushConstant::GetSize() const
{
	return sizeof(ModelWorld);
}

VkPushConstantRange Core::ModelPushConstant::GetPushConstantRange() const
{
	VkPushConstantRange pushConstant;
	pushConstant.offset = _offset;
	pushConstant.size = GetSize();
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	return pushConstant;
}

uint32_t Core::ModelPushConstant::GetOffset() const
{
	return _offset;
}

void Core::ModelPushConstant::SetOffset(uint32_t offset)
{
	_offset = offset;
}

Core::Mesh::Mesh(Entity& entity, VkCommandPool commandPool, string modelPath)
	:Component(entity)
{
	_modelPushConstant = new Core::ModelPushConstant();

	_texture = new Core::Texture(commandPool,
		"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha, 1);

	LoadModel(modelPath);

	CreateVertexBuffer(commandPool);
	CreateIndexBuffer(commandPool);
}

Core::Mesh::~Mesh()
{
	delete(_indexBuffer);
	delete(_vertexBuffer);
	delete(_texture);
	delete(_modelPushConstant);
}

void Core::Mesh::UpdateFrame(float deltaTime)
{
}

void Core::Mesh::DrawFrame(CommandBuffer& commandBuffer, Pipeline& pipeline) const
{
	auto cmd = commandBuffer.GetCommandBuffer();

	VkBuffer vertexBuffers[] = { _vertexBuffer->GetBuffer() };
	VkDeviceSize offsets[] = { 0 };

	auto& transform = _entity.GetComponent<Transform>();
	_modelPushConstant->Buffer.World = transform.GetMatrix();

	uint32_t index = commandBuffer.GetCurrentFrame();

	//hack : optimizable with https://developer.nvidia.com/vulkan-memory-management
	vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

	vkCmdBindIndexBuffer(cmd, _indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

	vkCmdPushConstants(cmd, pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, _modelPushConstant->GetOffset(), _modelPushConstant->GetSize(), &_modelPushConstant->Buffer);

	vkCmdDrawIndexed(cmd, static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
}

Core::IDescriptor* Core::Mesh::GetDescriptor() const
{
	return _texture;
}

Core::ModelPushConstant* Core::Mesh::GetPushConstant() const
{
	return _modelPushConstant;
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

void Core::Mesh::CreateVertexBuffer(VkCommandPool commandPool)
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

void Core::Mesh::CreateIndexBuffer(VkCommandPool commandPool)
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

std::type_index Core::Mesh::GetType()
{
	return typeid(Mesh);
}

void Core::Mesh::Resize(uint32_t width, uint32_t height)
{
}
