#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class ResourceManager;
	class Device;
	class RenderScene;
	class TerrainSystem;
	class Shader;
	class Pipeline;
	class Texture;

	// Renders this frame's shading cache pages (terrainPageRender.comp): the
	// near material blend of each requested page into the two cache
	// textures, which the terrain color draw then samples instead of
	// blending per pixel. Declares nothing when there is no page to render,
	// so the graph culls it.
	class TerrainPageRenderPass : public FrameGraphPass
	{
	public:
		TerrainPageRenderPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene);
		~TerrainPageRenderPass() override;

		const char* GetName() const override { return "TerrainPageRender"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		RenderScene& _renderScene;
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		Handle<Pipeline> _pipeline;

		Handle<Texture> _indexTexture;

		FGTexture _albedo, _normal;
		FGBuffer _params, _palette, _renderList;
		uint32_t _pageCount = 0;
		uint32_t _groupsPerPage = 0;
		bool _active = false;
	};
}
