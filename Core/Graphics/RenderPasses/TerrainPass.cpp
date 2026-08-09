#include "stdafx.h"
#include "TerrainPass.h"
#include "GeometryPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RenderScene.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Light.h"
#include "Components/Transform.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"

using namespace Core;

TerrainPass::TerrainPass(Device& device, RenderScene& renderScene,
	VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
	: _device(device)
	, _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	_shader = device.GetResourceManager().LoadShader("Terrain");

	_pipelineState = make_unique<PipelineState>();
	_pipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;
	// Heightfields have no closed backside; GPU patch cone culling replaces
	// this in the phase-2 pipeline.
	_pipelineState->GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_NONE;

	PipelineRenderingDesc renderingDesc;
	renderingDesc.colorFormats = { colorFormat };
	renderingDesc.depthFormat = depthFormat;
	_pipeline = make_unique<Pipeline>(device, renderingDesc, _shader.Get(), *_pipelineState);

	auto wireframeState = *_pipelineState;
	wireframeState.GetRasterizationStateCreateInfo().polygonMode = VK_POLYGON_MODE_LINE;
	_wireframePipeline = make_unique<Pipeline>(device, renderingDesc, _shader.Get(),
		wireframeState);
}

TerrainPass::~TerrainPass() = default;

void TerrainPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	const auto& renderList = _terrain.GetRenderList();
	_instanceCount = uint32_t(renderList.size());
	if (_instanceCount == 0)
		return; // nothing resident yet: the pass culls itself

	// The main targets only exist on frames the geometry pass declared them.
	if (!builder.HasTexture(GeometryPass::RT_MAIN_COLOR))
	{
		_instanceCount = 0;
		return;
	}

	_mainColor = builder.GetTexture(GeometryPass::RT_MAIN_COLOR);
	_mainDepth = builder.GetTexture(GeometryPass::RT_MAIN_DEPTH);

	FGAttachment color;
	color.texture = _mainColor;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetColorAttachment(0, color);

	FGAttachment depth;
	depth.texture = _mainDepth;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetDepthAttachment(depth);

	TerrainQuadTree& quadTree = _terrain.GetQuadTree();
	_height = builder.ImportTexture(TerrainQuadTree::HEIGHT_ATLAS, quadTree.GetHeightAtlas());
	builder.Read(_height, TextureAccess::SampledVertex);
	_normal = builder.ImportTexture(TerrainQuadTree::NORMAL_ATLAS, quadTree.GetNormalAtlas());
	builder.Read(_normal, TextureAccess::SampledFragment);
	_albedo = builder.ImportTexture(TerrainQuadTree::ALBEDO_ATLAS, quadTree.GetAlbedoAtlas());
	builder.Read(_albedo, TextureAccess::SampledFragment);

	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformVertex);

	_instances = builder.ImportBuffer("Terrain.Instances",
		frameResources.CreateOrReplaceStorageBuffer("Terrain.Instances",
			{ renderList.size() * sizeof(TerrainNodeInstance),
			  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT },
			renderList));
	builder.Read(_instances, BufferAccess::StorageVertexRead);

	const TerrainConfig& config = _terrain.GetConfig();
	TerrainParams params{};
	params.heightMinMaxInvAtlas = vec4(config.heightMin, config.heightMax,
		1.0f / vec2(quadTree.GetHeightAtlasExtent()));
	params.invColorAtlasBorder = vec4(1.0f / vec2(quadTree.GetColorAtlasExtent()),
		float(config.borderTexels), 0.0f);

	if (auto* mainLight = _renderScene.GetScene().GetMainLight())
	{
		auto& transform = mainLight->GetEntity().GetTransform();
		vec3 direction = transform.GetRotation() * mainLight->GetProperties().Direction;
		if (length(direction) > 0.0f)
			params.sunDirection = vec4(normalize(direction), 0.25f);
	}
	params.debugMode = ivec4(_terrain.GetDebugMode(), 0, 0, 0);

	auto paramsHandle = frameResources.GetOrCreateUniformBuffer<TerrainParams>("Terrain.Params");
	paramsHandle.Get().Update(params);
	_params = builder.ImportBuffer("Terrain.Params", paramsHandle);
	builder.Read(_params, BufferAccess::UniformFragment);
}

void TerrainPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (_instanceCount == 0)
		return;

	context.BeginRendering(commandBuffer);
	commandBuffer.SetViewportAndScissor(context.GetRenderArea());

	Pipeline* pipeline = _terrain.IsWireframe()
		? _wireframePipeline.get() : _pipeline.get();
	commandBuffer.BindPipeline(pipeline);

	Shader& shader = _shader.Get();
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetUniformBuffer(0, context.GetBuffer(_camera));
	builder.SetStorageBuffer(1, context.GetBuffer(_instances));
	builder.SetTextureBuffer(2, context.GetTexture(_height));
	builder.SetTextureBuffer(3, context.GetTexture(_normal));
	builder.SetTextureBuffer(4, context.GetTexture(_albedo));
	builder.SetUniformBuffer(5, context.GetBuffer(_params));
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(pipeline->GetPipelineBindPoint(), shader, resources);

	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);
	commandBuffer.DrawIndexed(_terrain.GetGridIndexCount(), _instanceCount);

	context.EndRendering(commandBuffer);
}
