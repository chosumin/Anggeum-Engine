#include "stdafx.h"
#include "Mesh.h"
#include "VulkanWrapper/Vertex.h"
#include "Utils/Utility.h"
#include "SubMesh.h"
#include "Material.h"
#include "MaterialFactory.h"
#include "Entity.h"

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
}

Core::Mesh::Mesh(Device& device, int polygonType)
	:_device(device)
{
	LoadPlane();
}

Core::Mesh::Mesh(Device& device)
	:_device(device)
{
}

Core::Mesh::~Mesh()
{
	for (auto&& subMesh : _subMeshes)
	{
		delete(subMesh);
	}
	_subMeshes.clear();
}

void Core::Mesh::AddSubMesh(int polygonType)
{
	LoadPlane();
}

void Core::Mesh::AddSubMesh(string path)
{
	LoadModel(path);
}

void Core::Mesh::AddSubMesh(SubMesh* subMesh)
{
	_subMeshes.push_back(subMesh);
}

void Core::Mesh::AddMaterial(Material* material)
{
	_materials.push_back(material);
}

void Core::Mesh::AddMaterial(string path)
{
	auto material = MaterialFactory::CreateMaterial(_device, path);
	_materials.push_back(material);
}

void Core::Mesh::LoadModel(const string& modelPath)
{
	SubMesh* subMesh = new SubMesh(_device, modelPath);

	tinyobj::attrib_t attrib;
	vector<tinyobj::shape_t> shapes;
	vector<tinyobj::material_t> materials;
	string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str()))
		throw runtime_error(warn + err);

	unordered_map<Vertex, uint32_t> uniqueVertices;

	unordered_map<string, vector<uint8_t>> objVertices;
	vector<uint8_t> indices;

	for (const auto& shape : shapes)
	{
		subMesh->SetIndexCount(Utility::ToU32(shape.mesh.indices.size()));

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
				auto vertices = objVertices.begin();
				if (vertices != objVertices.end())
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices->second.size() / sizeof(vec3));
				else
					uniqueVertices[vertex] = 0;

				AddVertex(objVertices, vertex);
			}
;
			auto indexBytes = Core::Utility::ToBytes(uniqueVertices[vertex]);
			indices.insert(indices.begin(), indexBytes.begin(), indexBytes.end());
		}
	}

	for (auto& vertices : objVertices)
	{
		subMesh->CreateVertexBuffer(vertices.first, vertices.second);
	}

	subMesh->CreateIndexBuffer(indices, VK_INDEX_TYPE_UINT16);

	_subMeshes.push_back(subMesh);
}

void Core::Mesh::LoadPlane()
{
	SubMesh* subMesh = new SubMesh(_device, "Plane");

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

	unordered_map<string, vector<uint8_t>> objVertices;
	vector<uint32_t> intIndices = { 0,1,2,2,1,3 };

	subMesh->SetIndexCount(6);

	for (auto&& vertex : vertices)
	{
		AddVertex(objVertices, vertex);
	}

	vector<uint8_t> byteIndices;
	for (auto&& index : intIndices)
	{
		auto indexBytes = Core::Utility::ToBytes(index);
		byteIndices.insert(byteIndices.begin(), indexBytes.begin(), indexBytes.end());
	}

	for (auto& vertices : objVertices)
	{
		subMesh->CreateVertexBuffer(vertices.first, vertices.second);
	}

	subMesh->CreateIndexBuffer(byteIndices, VK_INDEX_TYPE_UINT16);

	_subMeshes.push_back(subMesh);
}

void Core::Mesh::UpdateFrame(float deltaTime)
{
}

std::type_index Core::Mesh::GetType()
{
	return typeid(Mesh);
}

void Core::Mesh::Resize(uint32_t width, uint32_t height)
{
}
