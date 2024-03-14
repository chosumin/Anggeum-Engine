#include "stdafx.h"
#include "MeshRenderer.h"
#include "Pipeline.h"
#include "Material.h"
using namespace Core;

Core::MeshRenderer::~MeshRenderer()
{
	delete(_material);
	delete(_pipeline);
}

void Core::MeshRenderer::UpdateFrame(float deltaTime)
{
}

std::type_index Core::MeshRenderer::GetType()
{
	return std::type_index();
}

void Core::MeshRenderer::Resize(uint32_t width, uint32_t height)
{
}
