#include "stdafx.h"
#include "TerrainPageRenderPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RenderScene.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Graphics/Terrain/TerrainShadingCache.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TerrainPageRenderPass::TerrainPageRenderPass(Device& device, ResourceManager& resourceManager,
	RenderScene& renderScene)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	_shader = resourceManager.LoadShader("Shaders/Terrain/terrainPageRender.comp.spv");
	_pipeline = resourceManager.LoadComputePipeline("Shaders/Terrain/terrainPageRender.comp.spv");
}

TerrainPageRenderPass::~TerrainPageRenderPass() = default;

void TerrainPageRenderPass::Setup(FrameGraphBuilder& builder,
	FrameResources& frameResources, RenderFrame& renderFrame)
{
	_active = false;

	TerrainShadingCache& cache = _terrain.GetShadingCache();
	const vector<TerrainPageRenderGPU>& pages = cache.GetRenderList();
	if (!cache.IsEnabled() || pages.empty())
		return;

	_indexTexture = frameResources.GetRenderTarget(TerrainQuadTree::QUADTREE_INDEX);

	// The same per-frame blocks the terrain draws read; the renderer refreshes
	// them again with the viewport filled in, identical otherwise.
	auto paramsHandle = frameResources.GetOrCreateUniformBuffer<TerrainParams>(TerrainSystem::UB_PARAMS);
	paramsHandle.Get().Update(_terrain.BuildRenderParams());
	_params = builder.ImportBuffer(TerrainSystem::UB_PARAMS, paramsHandle);
	builder.Read(_params, BufferAccess::UniformCompute);

	auto paletteHandle = frameResources.GetOrCreateUniformBuffer<TerrainMaterialPaletteGPU>(
		TerrainSystem::UB_PALETTE);
	paletteHandle.Get().Update(_terrain.GetMaterials().BuildPalette());
	_palette = builder.ImportBuffer(TerrainSystem::UB_PALETTE, paletteHandle);
	builder.Read(_palette, BufferAccess::UniformCompute);

	TerrainPageRenderListGPU list{};
	_pageCount = std::min<uint32_t>(uint32_t(pages.size()), TERRAIN_CACHE_MAX_PAGE_RENDERS);
	copy(pages.begin(), pages.begin() + _pageCount, list.pages);
	list.count = uvec4(_pageCount, 0, 0, 0);
	auto listHandle = frameResources.GetOrCreateUniformBuffer<TerrainPageRenderListGPU>(
		TerrainShadingCache::UB_PAGE_RENDER_LIST);
	listHandle.Get().Update(list);
	_renderList = builder.ImportBuffer(TerrainShadingCache::UB_PAGE_RENDER_LIST, listHandle);
	builder.Read(_renderList, BufferAccess::UniformCompute);

	_albedo = builder.ImportTexture(TerrainShadingCache::ALBEDO_CACHE, cache.GetAlbedoCache());
	builder.Write(_albedo, TextureAccess::StorageComputeWrite);
	_normal = builder.ImportTexture(TerrainShadingCache::NORMAL_CACHE, cache.GetNormalCache());
	builder.Write(_normal, TextureAccess::StorageComputeWrite);

	_groupsPerPage = (_terrain.GetConfig().shadingCache.Stride() + 7) / 8;
	_active = true;
}

void TerrainPageRenderPass::Execute(FrameGraphPassContext& context,
	CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	Shader& shader = _shader.Get();
	Pipeline& pipeline = _pipeline.Get();
	commandBuffer.BindPipeline(&pipeline);

	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetTextureBuffer(0, _indexTexture.Get());
	builder.SetTextureBuffer(1, _terrain.GetQuadTree().GetSplatAtlas().Get(), 0,
		VK_IMAGE_LAYOUT_GENERAL);
	builder.SetUniformBuffer(2, context.GetBuffer(_params));
	builder.SetUniformBuffer(3, context.GetBuffer(_palette));
	builder.SetUniformBuffer(4, context.GetBuffer(_renderList));
	builder.SetTextureBuffer(5, context.GetTexture(_albedo), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetTextureBuffer(6, context.GetTexture(_normal), 0, VK_IMAGE_LAYOUT_GENERAL);
	auto& resources = builder.Build();

	// The detail maps come through the bindless set, like the color draw.
	BindlessTextureManager* bindless = _renderScene.GetBindlessTextureManager();
	assert(shader.UsesBindlessTextures() && bindless != nullptr);
	DescriptorSetResources bindlessResources{};
	bindlessResources.descriptorSet = bindless->GetDescriptorSet();
	bindlessResources.setIndex = static_cast<uint32_t>(DescriptorSetType::Bindless);

	commandBuffer.BindDescriptorSets(pipeline.GetPipelineBindPoint(), shader,
		{ &resources, &bindlessResources });

	commandBuffer.Dispatch(_groupsPerPage, _groupsPerPage, _pageCount);
}
