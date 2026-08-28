#pragma once
#include "Graphics/BufferObjects.h"

namespace Core
{
	class ResourceManager;
	class FrameGraph;
	class Device;
	class RenderScene;
	class ShadowPass;

	class ShadowPasses
	{
	public:
		ShadowPasses(FrameGraph& graph, Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
			VkFormat depthFormat);

		// CPU-side shadow block shared with SDFShadowPass (transition distances).
		ShadowUniform* GetShadowBuffer() const;

	private:
		ShadowPass* _shadowPass = nullptr; // graph-owned
	};
}
