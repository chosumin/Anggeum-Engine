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
#include "Graphics/Terrain/TerrainSystem.h"

using namespace Core;

RenderScene::RenderScene(Device& device, Scene& scene, TransferContext& transfer)
	: _device(&device)
	, _scene(scene)
	, _transfer(transfer)
{
	// Bindless textures require descriptor indexing; the rest are always created.
	if (device.SupportsDescriptorIndexing())
		_bindless = make_unique<BindlessTextureManager>(device, 4096);

	_meshBuffer = make_unique<MeshBufferManager>(device);
	_material = make_unique<MaterialManager>(device);
	_batch = make_unique<RendererBatch>(device);

	// The terrain's static grid geometry lands in the scheduler's geometry
	// queue and is uploaded by the first Sync, like any other loaded geometry.
	_terrainSystem = make_unique<TerrainSystem>(device, transfer.GetGeometryCopyQueue());
}

// Out of line for the unique_ptr members forward-declared in the header.
RenderScene::~RenderScene() = default;

void RenderScene::Sync(Scene& scene, VkExtent2D extents)
{
	// Every manager self-gates on its own dirty state; this only fixes the order.
	// Space was already reserved at load time, so the scheduler just turns the
	// pending requests into data-copy jobs...
	_transfer.SubmitQueued();

	// ...and flush them before the steps below: the bindless descriptor writes need
	// the uploaded textures' image views (created by the image jobs), and the draw
	// set references the uploaded geometry and the bounds those jobs computed.
	_transfer.Flush();

	ApplyComputedBounds();

	if (_bindless)
		_bindless->Sync();

	_material->Sync();

	// The batch pushes its table fills as copy requests like every loader, so
	// run the scheduler once more: the passes read the draw set on the GPU
	// during this frame.
	_batch->Sync(scene, _transfer.GetGeometryCopyQueue(), extents);
	_transfer.SubmitQueued();
	_transfer.Flush();

	// Freezes this frame's terrain streaming uploads and render list before pass Setup.
	if (auto* camera = scene.GetMainCamera())
		_terrainSystem->Update(*camera);
}

GeometryCopyQueue& RenderScene::GetGeometryCopyQueue()
{
	return _transfer.GetGeometryCopyQueue();
}

TextureUploadQueue& RenderScene::GetTextureUploadQueue()
{
	return _transfer.GetTextureUploadQueue();
}

void RenderScene::ApplyComputedBounds()
{
	// The scene mirror's side of the bounds hand-off: the scheduler's jobs
	// measured them, the mesh and the scene-wide bounds live here.
	for (auto& completed : _transfer.TakeCompletedBounds())
	{
		completed.target->SetBoundingSphere(completed.bounds.center, completed.bounds.radius);

		_sceneBoundsMin = glm::min(_sceneBoundsMin, completed.bounds.min);
		_sceneBoundsMax = glm::max(_sceneBoundsMax, completed.bounds.max);
	}
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
