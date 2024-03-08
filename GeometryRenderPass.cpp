#include "stdafx.h"
#include "GeometryRenderPass.h"
#include "Mesh.h"
#include "Scene.h"

namespace Core
{
	GeometryRenderPass::GeometryRenderPass(Device& device, VkExtent2D extent, 
		VkFormat colorFormat, Scene& scene)
		:RenderPass(device), _scene(scene)
	{
		CreateColorRenderTarget(extent, colorFormat);
		CreateDepthRenderTarget(extent);
		CreateRenderPass();
	}

	GeometryRenderPass::~GeometryRenderPass()
	{
	}

	void GeometryRenderPass::Prepare()
	{
	}

	void GeometryRenderPass::Draw()
	{
		auto meshes = _scene.GetComponents<Core::Mesh>();

		//todo : sort
		//todo : render
	}
}
