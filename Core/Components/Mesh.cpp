#include "stdafx.h"
#include "Mesh.h"
#include "Utils/Utility.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/Vertex.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
#include "Foundation/Entity.h"

Core::Mesh::Mesh(Device& device)
	:_device(device)
{
}

Core::Mesh::~Mesh()
{
	auto& resourceCache = _device.GetResourceCache();

	for (auto&& subMesh : _subMeshes)
	{
		delete(subMesh);
	}
	_subMeshes.clear();

	for (auto&& material : _materials)
	{
		resourceCache.ReleaseMaterial(material);
	}
	_materials.clear();
}

void Core::Mesh::AddSubMesh(SubMesh* subMesh)
{
	_subMeshes.push_back(subMesh);
}

void Core::Mesh::AddMaterial(shared_ptr<Material> material)
{
	_materials.push_back(material);
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
