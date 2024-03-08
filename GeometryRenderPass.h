#pragma once
#include "RenderPass.h"

namespace Core
{
	class Scene;
	class Camera;
	class GeometryRenderPass : public RenderPass
	{
	public:
		GeometryRenderPass(Device& device, VkExtent2D extent, VkFormat colorFormat,
			Scene& scene);
		virtual ~GeometryRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw() override;
	private:
		Scene& _scene;
	};
}

