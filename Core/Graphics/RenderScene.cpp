#include "stdafx.h"
#include "RenderScene.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/SubMesh.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/TransferContext.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/Terrain/TerrainSystem.h"

using namespace Core;

RenderScene::RenderScene(Device& device, Scene& scene, TransferContext& transfer)
	: _device(&device)
	, _scene(scene)
{
	// Bindless textures require descriptor indexing; the rest are always created.
	if (device.SupportsDescriptorIndexing())
		_bindless = make_unique<BindlessTextureManager>(device, 4096);

	_meshBuffer = make_unique<MeshBufferManager>(device);
	_material = make_unique<MaterialManager>(device);
	_batch = make_unique<RendererBatch>(device);

	// The terrain's static grid geometry lands in the transfer context's
	// geometry queue and is uploaded by the first Sync, like any other loaded
	// geometry; its streamer stages tiles from the context's ring.
	_terrainSystem = make_unique<TerrainSystem>(device, transfer);
}

// Out of line for the unique_ptr members forward-declared in the header.
RenderScene::~RenderScene() = default;

void RenderScene::SyncManagers(Scene& scene, GeometryCopyQueue& copyQueue,
	VkExtent2D extents, uint32_t promotedCount)
{
	// Newly-Resident geometry can only join the draw set through a rebuild.
	if (promotedCount > 0)
		_batch->MarkDirty();

	if (_bindless)
		_bindless->Sync();

	_material->Sync();

	_batch->Sync(scene, copyQueue, extents);

	if (auto* camera = scene.GetMainCamera())
		_terrainSystem->Update(*camera);
}

void RenderScene::DrawIndirect(CommandBuffer& commandBuffer, Shader& shader,
	Pipeline& pipeline, Buffer& indirectCommandBuffer, DescriptorSetBuilder& builder)
{
	if (_batch->GetDrawCommandCount() == 0)
		return;

	auto vertexAttibuteNames = shader.GetVertexAttirbuteNames();

	auto vertexBufferHandles = _meshBuffer->GetVertexBuffers(vertexAttibuteNames);
	vector<Buffer*> vertexBuffers;
	vertexBuffers.reserve(vertexBufferHandles.size());
	for (auto& handle : vertexBufferHandles)
		vertexBuffers.push_back(&handle.Get());

	commandBuffer.BindVertexBuffers(vertexBuffers, 0);
	commandBuffer.BindIndexBuffer(_meshBuffer->GetIndexBuffer().Get(), _meshBuffer->GetIndexType());

	commandBuffer.BindPipeline(&pipeline);

	builder.SetStorageBuffer(1, _batch->GetTransformBatch().TransformBuffer.Get());
	builder.SetStorageBuffer(2, _batch->GetInstanceBuffer());
	builder.SetUniformBuffer(8, _material->GetMaterialBuffer());
	builder.SetStorageBuffer(9, _batch->GetMaterialIndexBuffer());

	auto& resources = builder.Build();

	// Local on purpose: passes record on different workers concurrently, so the
	// bindless set reference must not go through shared mutable state.
	DescriptorSetResources bindlessResources{};
	vector<DescriptorSetResources*> resourcesList = { &resources };
	if (shader.UsesBindlessTextures() && _bindless != nullptr)
	{
		bindlessResources.descriptorSet = _bindless->GetDescriptorSet();
		bindlessResources.setIndex = static_cast<uint32_t>(DescriptorSetType::Bindless);
		resourcesList.push_back(&bindlessResources);
	}

	commandBuffer.BindDescriptorSets(pipeline.GetPipelineBindPoint(), shader, resourcesList);

	commandBuffer.DrawIndexedIndirect(
		indirectCommandBuffer,
		_batch->GetDrawCommandCount(),
		static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
	);
}
