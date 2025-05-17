#include "stdafx.h"
#include "Mesh.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Material.h"

Core::Mesh::Mesh(Device& device)
	:_device(device)
{
}

Core::Mesh::~Mesh()
{
	_subMeshes.clear();
	_materials.clear();
}

void Core::Mesh::AddSubMesh(shared_ptr<SubMesh> subMesh)
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
