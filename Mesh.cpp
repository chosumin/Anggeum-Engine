#include "stdafx.h"
#include "Mesh.h"
#include "Vertex.h"
#include "CommandBuffer.h"
#include "Buffer.h"
#include "Transform.h"
#include "MVPUniformBuffer.h"
#include "InputEvents.h"
#include "Texture.h"
#include "IDescriptor.h"
#include "Entity.h"
#include "Pipeline.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Core::Mesh::Mesh(Entity& entity, VkCommandPool commandPool, string modelPath)
	:Component(entity)
{
	_buffer = new Core::ModelUniformBuffer();

	_texture = new Core::Texture(commandPool,
		"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha);

	LoadModel(modelPath);

	CreateVertexBuffer(commandPool);
	CreateIndexBuffer(commandPool);
}

Core::Mesh::~Mesh()
{
	delete(_indexBuffer);
	delete(_vertexBuffer);
	delete(_texture);
	delete(_buffer);
}

void Core::Mesh::UpdateFrame(float deltaTime)
{
	/*auto& transform = _entity.GetComponent<Transform>();
	if (Core::Input::KeyPressed[KeyCode::B])
	{
		_type = _type == 0 ? 1 : 0;
		quat q = quat();
		transform.SetRotation(q);
	}

	if (_type == 0)
	{
		vec3 deltaRotation(0.0f, 0.0f, 0.0f);
		deltaRotation.y -= 1.0f;
		deltaRotation *= deltaTime;
		glm::quat qx = glm::angleAxis(deltaRotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::quat qy = glm::angleAxis(deltaRotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::quat orientation = glm::normalize(qy * transform.GetRotation() * qx);
		transform.SetRotation(orientation);
	}
	else
	{
		auto& rot = transform.GetRotation();
		auto euler = eulerAngles(rot);
		float angle = euler.y - deltaTime;
		auto a = rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
		transform.SetMatrix(a);
	}

	_buffer->BufferObject = transform.GetMatrix();*/
}

void Core::Mesh::DrawFrame(CommandBuffer& commandBuffer, Pipeline& pipeline) const
{
	auto cmd = commandBuffer.GetCommandBuffer();

	VkBuffer vertexBuffers[] = { _vertexBuffer->GetBuffer() };
	VkDeviceSize offsets[] = { 0 };

	//hack : optimizable with https://developer.nvidia.com/vulkan-memory-management
	vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

	vkCmdBindIndexBuffer(cmd, _indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

	auto& transform = _entity.GetComponent<Transform>();

	transform.SetTranslation(vec3());
	_buffer->BufferObject = transform.GetMatrix();
	{
		uint32_t index = commandBuffer.GetCurrentFrame();

		_buffer->Update(index);

		vkCmdBindDescriptorSets(
			cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline.GetPipelineLayout(), 0, 1,
			pipeline.GetDescriptorSet(index), 0, nullptr);

		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
	}

	transform.SetTranslation(vec3(1.0f, 0.0f, 0.0f));
	_buffer->BufferObject = transform.GetMatrix();
	{
		uint32_t index = commandBuffer.GetCurrentFrame();

		_buffer->Update(index);

		vkCmdBindDescriptorSets(
			cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline.GetPipelineLayout(), 0, 1,
			pipeline.GetDescriptorSet(index), 0, nullptr);

		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
	}
}

vector<Core::IDescriptor*> Core::Mesh::GetDescriptors() const
{
	vector<Core::IDescriptor*> vec;
	vec.push_back(_buffer);
	vec.push_back(_texture);
	return vec;
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
