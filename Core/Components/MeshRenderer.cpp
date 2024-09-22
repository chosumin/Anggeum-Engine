#include "stdafx.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialFactory.h"
#include "Mesh.h"
#include "MeshFactory.h"
#include "Entity.h"
using namespace Core;

Core::MeshRenderer::MeshRenderer(Device& device, Entity& entity)
	:Component(entity), _device(device)
{
}

Core::MeshRenderer::~MeshRenderer()
{
}

void Core::MeshRenderer::SetMesh(int polygonType)
{
	_mesh = MeshFactory::CreateMesh(_device, polygonType);
}

void Core::MeshRenderer::SetMesh(string path)
{
	_mesh = MeshFactory::CreateMesh(_device, path);
}

void Core::MeshRenderer::SetMaterial(string path)
{
	_material = MaterialFactory::CreateMaterial(_device, path);
}

void Core::MeshRenderer::UpdateFrame(float deltaTime)
{
}

std::type_index Core::MeshRenderer::GetType()
{
	return typeid(MeshRenderer);
}

void Core::MeshRenderer::Resize(uint32_t width, uint32_t height)
{
	//do nothing.
}
