#include "stdafx.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Foundation/Entity.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"
#include "Vulkans/Texture.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "TransferJob.h"

using namespace Core;

Core::RendererBatches::RendererBatches(Device& device, TransformBatch& transformBatch)
	: _device(device)
	, _transformBatch(transformBatch)
	, _instanceBuffer(VK_NULL_HANDLE), _instanceCount(0)
{
}

Core::RendererBatches::~RendererBatches()
{
	if (_materialIndexBuffer != VK_NULL_HANDLE) delete(_materialIndexBuffer);
	if (_indirectCommandBuffer != VK_NULL_HANDLE) delete(_indirectCommandBuffer);
	if (_instanceBuffer != VK_NULL_HANDLE) delete(_instanceBuffer);
	if (_objectDataBuffer != VK_NULL_HANDLE) delete(_objectDataBuffer);

	// 2-Pass resources
	if (_rejectedIndicesBuffer != nullptr) delete(_rejectedIndicesBuffer);
	if (_rejectedCountBuffer != nullptr) delete(_rejectedCountBuffer);
	if (_pass2IndirectCommandBuffer != nullptr) delete(_pass2IndirectCommandBuffer);

	for (auto& batch : _shaderBatches)
	{
		delete(batch.second.Pipeline);
	}
	_shaderBatches.clear();
}

void Core::RendererBatches::Prepare(Device& device, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes)
{
	for (auto&& mesh : meshes)
	{
		uint entityId = mesh->GetEntity().GetId();

		auto materials = mesh->GetMaterials();
		for (size_t i = 0; i < materials.size(); ++i)
		{
			// Skip materials that don't have the "Geometry" pass
			if (materials[i]->GetShader().GetPass() != "Geometry")
				continue;

			auto& material = materials[i];
			auto& subMesh = mesh->GetSubMeshes()[i];

			AddBatch(device, renderPass, pipelineState, entityId, material, subMesh);
		}
	}

	CreateInstanceBuffer(device);
}

void Core::RendererBatches::PrepareGPUDrivenRendering(Device& device, bool needMaterialData,
	VkExtent2D extents)
{
	_needsMaterialIndexBuffer = needMaterialData;

	_indirectDrawBuffer.Clear();
	uint32_t globalFirstInstance = 0;
	uint32_t drawCommandIndex = 0;

	vector<GPUObjectData> objectData;
	objectData.reserve(_instanceCount);

	for (auto& [shaderHash, shaderBatch] : _shaderBatches)
	{
		for (auto& [materialName, materialBatch] : shaderBatch.MaterialBatches)
		{
			auto material = materialBatch.Material.lock();
			if (!material || !material->HasMaterialIndex())
				continue;

			uint32_t materialIndex = material->GetMaterialIndex();

			for (auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
			{
				auto subMesh = subMeshBatch.SubMesh.lock();
				if (!subMesh || !subMesh->HasAllocation())
					continue;

				const auto& allocation = subMesh->GetAllocation();
				uint32_t instanceCount = static_cast<uint32_t>(subMeshBatch.Transforms.size());

				_indirectDrawBuffer.AddDrawCommand(
					allocation,
					materialIndex,
					_needsMaterialIndexBuffer ? 0 : instanceCount,
					globalFirstInstance
				);

				// Prepare object data for each instance
				for (size_t i = 0; i < subMeshBatch.Transforms.size(); ++i)
				{
					GPUObjectData data{};
					data.boundingSphere = glm::vec4(
						allocation.boundingSphereCenter,
						allocation.boundingSphereRadius);
					data.transformIndex = subMeshBatch.Transforms[i];
					data.drawCommandIndex = drawCommandIndex;

					objectData.push_back(data);
				}

				globalFirstInstance += instanceCount;
				drawCommandIndex++;
			}
		}
	}

	vector<Job*> jobs;

	// Indirect Command Buffer
	Core::VkBufferJob<DrawIndexedIndirectCommand> job(device,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		&_indirectCommandBuffer,
		_indirectDrawBuffer.GetDrawCommands(), 0);
	jobs.push_back(&job);

	// Material Index SSBO
	Core::VkBufferJob<uint32_t> job2(device,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		&_materialIndexBuffer,
		_indirectDrawBuffer.GetMaterialIndices(), 0);
	jobs.push_back(&job2);

	// Object Data SSBO
	Core::VkBufferJob<GPUObjectData> job3(device,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		&_objectDataBuffer,
		objectData, 0);
	jobs.push_back(&job3);

	Core::CommandBuffer::ImmediateSubmit(device, jobs);

	PrepareHiZResources(device, extents);
	PrepareCullingResources(device);
}

void Core::RendererBatches::PrepareCullingResources(Core::Device& device)
{
	_cullingShader = device.GetResourceCache().RequestShader("Shaders/gpuCulling.comp.spv");
	_cullingPipeline = make_unique<Pipeline>(device, *_cullingShader);

	uint32_t drawCount = _indirectDrawBuffer.GetDrawCount();

	_rejectedIndicesBuffer = new Core::Buffer(device,
		_instanceCount * sizeof(uint32_t),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL);

	_rejectedCountBuffer = new Core::Buffer(device,
		sizeof(uint32_t),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL);

	// Pass 2 Indirect Command Buffer
	Core::VkBufferJob<DrawIndexedIndirectCommand> pass2Job(device,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		&_pass2IndirectCommandBuffer,
		_indirectDrawBuffer.GetDrawCommands(), 0);
	Core::CommandBuffer::ImmediateSubmit(device, pass2Job);

	_pass2CullingShader = device.GetResourceCache().RequestShader("Shaders/gpuCullingPass2.comp.spv");
	_pass2CullingPipeline = make_unique<Pipeline>(device, *_pass2CullingShader);

	// Reset draw commands shader (2-pass)
	_resetDrawCommandsShader = device.GetResourceCache().RequestShader("Shaders/resetDrawCommands.comp.spv");
	_resetDrawCommandsPipeline = make_unique<Pipeline>(device, *_resetDrawCommandsShader);

	// Frustum-only culling resources
	_frustumCullingShader = device.GetResourceCache().RequestShader("Shaders/frustumCulling.comp.spv");
	_frustumCullingPipeline = make_unique<Pipeline>(device, *_frustumCullingShader);

	_resetDrawCommandsSimpleShader = device.GetResourceCache().RequestShader("Shaders/resetDrawCommandsSimple.comp.spv");
	_resetDrawCommandsSimplePipeline = make_unique<Pipeline>(device, *_resetDrawCommandsSimpleShader);
}

void Core::RendererBatches::PrepareSingleBatch(Device& device, weak_ptr<Material> material, RenderPass& renderPass, PipelineState& pipelineState, vector<Mesh*>& meshes)
{
	for (auto&& mesh : meshes)
	{
		uint entityId = mesh->GetEntity().GetId();
		auto& subMeshes = mesh->GetSubMeshes();

		for (auto& subMesh : subMeshes)
		{
			AddBatch(device, renderPass, pipelineState, entityId, material, subMesh);
		}
	}

	CreateInstanceBuffer(device);
}

void Core::RendererBatches::ResetDrawCommands(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
	uint32_t drawCount = _indirectDrawBuffer.GetDrawCount();

	commandBuffer.BindPipeline(_resetDrawCommandsPipeline.get());

	renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsShader, 0, _indirectCommandBuffer);
	renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsShader, 1, _pass2IndirectCommandBuffer);
	renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsShader, 2, _rejectedCountBuffer);

	commandBuffer.PushConstants(*_resetDrawCommandsShader, 0, &drawCount);
	commandBuffer.BindDescriptorSets(renderFrame,
		_resetDrawCommandsPipeline->GetPipelineBindPoint(), *_resetDrawCommandsShader);

	uint32_t groupCount = (drawCount + 63) / 64;
	commandBuffer.Dispatch(std::max(1u, groupCount), 1, 1);

	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

void Core::RendererBatches::DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
	const CameraBuffer& camera, shared_ptr<Texture> depth,
	Core::Buffer* indirectCommandBuffer,
	shared_ptr<Shader> cullingShader, Pipeline* cullingPipeline)
{
	// Generate Hi-Z from depth
	if (!_hiZInitialized)
	{
		auto& hiZImage = *_hiZTexture->GetImage().lock();
		commandBuffer.TransitionImageLayout(hiZImage,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		_hiZInitialized = true;
	}
	else
	{
		GenerateHiZBuffer(renderFrame, commandBuffer, depth);
	}

	// Culling dispatch
	GPUCullData cullData{};
	cullData.view = camera.View;
	cullData.proj = camera.Projection;
	cullData.screenSize = glm::vec2(_screenExtent.width, _screenExtent.height);
	cullData.drawCount = _instanceCount;
	cullData.hiZMipLevels = _hiZMipLevels;
	cullData.enableOcclusionCulling = _hiZInitialized ? 1 : 0;

	glm::mat4 viewProj = camera.Projection * camera.View;
	ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

	commandBuffer.BindPipeline(cullingPipeline);

	renderFrame.SetShaderUniformBuffer(*cullingShader, 0, &cullData);
	renderFrame.SetShaderStorageBuffer(*cullingShader, 1, _objectDataBuffer);
	renderFrame.SetShaderStorageBuffer(*cullingShader, 2, _transformBatch.TransformBuffer);
	renderFrame.SetShaderStorageBuffer(*cullingShader, 3, _instanceBuffer);
	renderFrame.SetShaderStorageBuffer(*cullingShader, 4, indirectCommandBuffer);
	renderFrame.SetShaderTextureBuffer(*cullingShader, 5, _hiZTexture);

	renderFrame.SetShaderStorageBuffer(*cullingShader, 10, _rejectedIndicesBuffer);
	renderFrame.SetShaderStorageBuffer(*cullingShader, 11, _rejectedCountBuffer);

	commandBuffer.BindDescriptorSets(renderFrame,
		cullingPipeline->GetPipelineBindPoint(), *cullingShader);

	uint32_t groupCount = (_instanceCount + 63) / 64;
	commandBuffer.Dispatch(groupCount, 1, 1);

	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
}

void Core::RendererBatches::GpuDrivenDraw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, 
	shared_ptr<Texture> prevDepth, shared_ptr<Texture> curDepth, 
	CameraBuffer& camera,
	Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
	Framebuffer& framebuffer,
	function<void(shared_ptr<Shader>)> perShader, function<void(shared_ptr<Material>)> perDraw,
	function<void()> postDraw)
{
	commandBuffer.BeginDebugMarker("Reset Draw Commands");
	ResetDrawCommands(renderFrame, commandBuffer);
	commandBuffer.EndDebugMarker();
	
	commandBuffer.BeginDebugMarker("Pass 1 Culling");
	DispatchCulling(renderFrame, commandBuffer, camera, prevDepth,
		_indirectCommandBuffer, _cullingShader, _cullingPipeline.get());
	commandBuffer.EndDebugMarker();

	commandBuffer.BeginDebugMarker("Pass 1 Render Visible Objects");
	auto pass1BeginInfo = pass1RenderPass.CreateRenderPassBeginInfo(framebuffer);
	commandBuffer.BeginRenderPass(pass1BeginInfo);
	DrawIndirect(renderFrame, commandBuffer, *_indirectCommandBuffer, perShader, perDraw);
	commandBuffer.EndRenderPass();
	commandBuffer.EndDebugMarker();

	commandBuffer.BeginDebugMarker("Pass 2 Culling");
	DispatchCulling(renderFrame, commandBuffer, camera, curDepth,
		_pass2IndirectCommandBuffer, _pass2CullingShader, _pass2CullingPipeline.get());
	commandBuffer.EndDebugMarker();

	commandBuffer.BeginDebugMarker("Pass 2 Render Newly Visible Objects");
	auto pass2BeginInfo = pass2RenderPass.CreateRenderPassBeginInfo(framebuffer);
	commandBuffer.BeginRenderPass(pass2BeginInfo);
	DrawIndirect(renderFrame, commandBuffer, *_pass2IndirectCommandBuffer, perShader, perDraw);

	if (postDraw)
	{
		commandBuffer.BeginDebugMarker("Post Draw");
		postDraw();
		commandBuffer.EndDebugMarker();
	}

	commandBuffer.EndRenderPass();
	commandBuffer.EndDebugMarker();
}

void Core::RendererBatches::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
	DescriptorSetBuilder& builder,
	function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw)
{
	for (auto&& shaderBatch : _shaderBatches)
	{
		commandBuffer.BindPipeline(shaderBatch.second.Pipeline);

		auto shader = shaderBatch.second.SharedShader.lock();

		// Batch fills its bindings, then builds
		builder.SetStorageBuffer(1, _transformBatch.TransformBuffer);
		builder.SetStorageBuffer(2, _instanceBuffer);
		auto& resources = builder.Build();

		commandBuffer.BindDescriptorSet(
			renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS,
			*shader, builder.GetSetIndex(), resources);

		for (auto&& materialBatch : shaderBatch.second.MaterialBatches)
		{
			auto sharedMaterial = materialBatch.second.Material.lock();

			commandBuffer.BindDescriptorSets(
				renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS, *sharedMaterial);

			for (auto&& subMeshBatch : materialBatch.second.SubMeshBatches)
			{
				auto subMesh = subMeshBatch.second.SubMesh.lock();

				perDraw(sharedMaterial, subMesh);

				auto vertexAttibuteNames = sharedMaterial->GetShader().GetVertexAttirbuteNames();

				commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);
				commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());
				commandBuffer.DrawIndexed(
					subMesh->GetIndexCount(),
					subMeshBatch.second.Transforms.size(),
					subMeshBatch.second.FirstInstance);
			}
		}
	}
}

void Core::RendererBatches::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    function<void(shared_ptr<Shader>)> perShader, 
    function<void(shared_ptr<Material>, shared_ptr<SubMesh>)> perDraw)
{
    for (auto&& shaderBatch : _shaderBatches)
    {
        commandBuffer.BindPipeline(shaderBatch.second.Pipeline);

		auto shader = shaderBatch.second.SharedShader.lock();
		perShader(shader);

		renderFrame.SetShaderStorageBuffer(*shader, 1, _transformBatch.TransformBuffer);
		renderFrame.SetShaderStorageBuffer(*shader, 2, _instanceBuffer);

		commandBuffer.BindDescriptorSets(
			renderFrame,
			VK_PIPELINE_BIND_POINT_GRAPHICS, *shader);

        for (auto&& materialBatch : shaderBatch.second.MaterialBatches)
		{
			auto sharedMaterial = materialBatch.second.Material.lock();

			commandBuffer.BindDescriptorSets(
				renderFrame,
				VK_PIPELINE_BIND_POINT_GRAPHICS, *sharedMaterial);

            for (auto&& subMeshBatch : materialBatch.second.SubMeshBatches)
            {
                auto subMesh = subMeshBatch.second.SubMesh.lock();

                perDraw(sharedMaterial, subMesh);

                auto vertexAttibuteNames = sharedMaterial->GetShader().GetVertexAttirbuteNames();

                commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);
                commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());
                commandBuffer.DrawIndexed(
                    subMesh->GetIndexCount(), 
                    subMeshBatch.second.Transforms.size(),
                    subMeshBatch.second.FirstInstance);
            }
        }
    }
}

void Core::RendererBatches::DrawIndirect(
	RenderFrame& renderFrame,
	CommandBuffer& commandBuffer,
	function<void(shared_ptr<Shader>)> perShader,
	function<void(shared_ptr<Material>)> perDraw)
{
	DrawIndirect(renderFrame, commandBuffer, *_indirectCommandBuffer, perShader, perDraw);
}

void Core::RendererBatches::DrawIndirect(RenderFrame& renderFrame, CommandBuffer& commandBuffer, DescriptorSetBuilder& builder, function<void(shared_ptr<Material>)> perDraw)
{
	if (_indirectDrawBuffer.GetDrawCount() == 0)
		return;

	auto* meshBufferManager = renderFrame.GetMeshBufferManager();

	for (auto& [shaderHash, shaderBatch] : _shaderBatches)
	{
		auto shader = shaderBatch.SharedShader.lock();
		if (!shader)
			continue;

		auto vertexAttibuteNames = shader->GetVertexAttirbuteNames();
		commandBuffer.BindVertexBuffers(meshBufferManager->GetVertexBuffers(vertexAttibuteNames), 0);
		commandBuffer.BindIndexBuffer(meshBufferManager->GetIndexBuffer(), meshBufferManager->GetIndexType());

		commandBuffer.BindPipeline(shaderBatch.Pipeline);

		// Batch fills its bindings, then builds
		builder.SetStorageBuffer(1, _transformBatch.TransformBuffer);
		builder.SetStorageBuffer(2, _instanceBuffer);

		if (_needsMaterialIndexBuffer)
		{
			builder.SetUniformBuffer(8,
				const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
			builder.SetStorageBuffer(9, _materialIndexBuffer);
		}

		auto& resources = builder.Build();

		commandBuffer.BindDescriptorSet(
			renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS,
			*shader, builder.GetSetIndex(), resources);

		auto material = shaderBatch.MaterialBatches.begin()->second.Material.lock();
		perDraw(material);

		commandBuffer.DrawIndexedIndirect(
			*_indirectCommandBuffer,
			_indirectDrawBuffer.GetDrawCount(),
			static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
		);
	}
}

void Core::RendererBatches::AddBatch(Device& device, RenderPass& renderPass, PipelineState& pipelineState, uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh)
{
	auto materialPtr = material.lock();
	auto shaderPtr = materialPtr->GetShaderPtr().lock();

	auto key = shaderPtr->GetType();
	auto batch = _shaderBatches.find(key);
	if (batch == _shaderBatches.end())
	{
		// Add new shader batch
		ShaderBatch shaderBatch;

		shaderBatch.Pipeline = new Core::Pipeline(device, renderPass, *shaderPtr.get(), pipelineState);
		shaderBatch.SharedShader = shaderPtr;
		_shaderBatches[key] = shaderBatch;
	}

	auto materialName = materialPtr->GetName();

	auto& materialBatches = _shaderBatches[key].MaterialBatches;
	auto matPtr = materialBatches.find(materialName);
	if (matPtr == materialBatches.end())
	{
		// Add new material batch
		MaterialBatch materialBatch;
		materialBatch.Material = material;

		materialBatches.insert(make_pair(materialName, materialBatch));
	}

	auto& subMeshBatches = materialBatches[materialName].SubMeshBatches;

	auto subMeshPtr = subMesh.lock();
	string subMeshName = subMeshPtr->GetName();

	auto& subMeshBatch = subMeshBatches[subMeshName];
	
	// Set the firstInstance if its the first time adding this submesh batch
	if (subMeshBatch.Transforms.empty())
	{
		subMeshBatch.SubMesh = subMesh;
		subMeshBatch.FirstInstance = _instanceCount;
	}

	subMeshBatch.Transforms.push_back(entityId);
	_instanceCount++;
}

void Core::RendererBatches::CreateInstanceBuffer(Device& device)
{
	vector<uint> instanceData(_instanceCount);
	
	uint i = 0;
	for (auto& shaderBatch : _shaderBatches)
	{
		for (auto& materialBatch : shaderBatch.second.MaterialBatches)
		{
			for (auto& subMeshBatch : materialBatch.second.SubMeshBatches)
			{
				for (auto& transform : subMeshBatch.second.Transforms)
				{
					instanceData[i++] = transform;
				}
			}
		}
	}

	_instanceBuffer = new Core::Buffer(device, _instanceCount * sizeof(uint),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::DEVICE_LOCAL);

	Core::VkBufferJob<uint> job(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &_instanceBuffer, instanceData, true);
	Core::CommandBuffer::ImmediateSubmit(device, job);
}

void Core::RendererBatches::ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes)
{
	// Left
	planes[0] = glm::vec4(
		viewProj[0][3] + viewProj[0][0],
		viewProj[1][3] + viewProj[1][0],
		viewProj[2][3] + viewProj[2][0],
		viewProj[3][3] + viewProj[3][0]);

	// Right
	planes[1] = glm::vec4(
		viewProj[0][3] - viewProj[0][0],
		viewProj[1][3] - viewProj[1][0],
		viewProj[2][3] - viewProj[2][0],
		viewProj[3][3] - viewProj[3][0]);

	// Bottom
	planes[2] = glm::vec4(
		viewProj[0][3] + viewProj[0][1],
		viewProj[1][3] + viewProj[1][1],
		viewProj[2][3] + viewProj[2][1],
		viewProj[3][3] + viewProj[3][1]);

	// Top
	planes[3] = glm::vec4(
		viewProj[0][3] - viewProj[0][1],
		viewProj[1][3] - viewProj[1][1],
		viewProj[2][3] - viewProj[2][1],
		viewProj[3][3] - viewProj[3][1]);

	// Near
	planes[4] = glm::vec4(
		viewProj[0][3] + viewProj[0][2],
		viewProj[1][3] + viewProj[1][2],
		viewProj[2][3] + viewProj[2][2],
		viewProj[3][3] + viewProj[3][2]);

	// Far
	planes[5] = glm::vec4(
		viewProj[0][3] - viewProj[0][2],
		viewProj[1][3] - viewProj[1][2],
		viewProj[2][3] - viewProj[2][2],
		viewProj[3][3] - viewProj[3][2]);

	// Normalize planes
	for (int i = 0; i < 6; ++i)
	{
		float length = glm::length(glm::vec3(planes[i]));
		planes[i] /= length;
	}
}

void Core::RendererBatches::DrawIndirect(RenderFrame& renderFrame, CommandBuffer& commandBuffer, Core::Buffer& indirectCommandBuffer, function<void(shared_ptr<Shader>)> perShader, function<void(shared_ptr<Material>)> perDraw)
{
	if (_indirectDrawBuffer.GetDrawCount() == 0)
		return;

	auto* meshBufferManager = renderFrame.GetMeshBufferManager();

	for (auto& [shaderHash, shaderBatch] : _shaderBatches)
	{
		auto shader = shaderBatch.SharedShader.lock();
		if (!shader)
			continue;

		auto vertexAttibuteNames = shader->GetVertexAttirbuteNames();
		commandBuffer.BindVertexBuffers(meshBufferManager->GetVertexBuffers(vertexAttibuteNames), 0);
		commandBuffer.BindIndexBuffer(meshBufferManager->GetIndexBuffer(), meshBufferManager->GetIndexType());

		commandBuffer.BindPipeline(shaderBatch.Pipeline);

		renderFrame.SetShaderStorageBuffer(*shader, 1, _transformBatch.TransformBuffer);
		renderFrame.SetShaderStorageBuffer(*shader, 2, _instanceBuffer);

		if (_needsMaterialIndexBuffer)
		{
			renderFrame.SetShaderUniformBuffer(*shader, 8, const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
			renderFrame.SetShaderStorageBuffer(*shader, 9, _materialIndexBuffer);
		}

		if (perShader)
			perShader(shader);

		commandBuffer.BindDescriptorSets(renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS, *shader);

		auto material = shaderBatch.MaterialBatches.begin()->second.Material.lock();

		perDraw(material);

		commandBuffer.DrawIndexedIndirect(
			indirectCommandBuffer,
			_indirectDrawBuffer.GetDrawCount(),
			static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
		);
	}
}

void Core::RendererBatches::PrepareHiZResources(Device& device, VkExtent2D extents)
{
    _screenExtent = extents;

    // Calculate mip levels
    uint32_t maxDim = std::max(_screenExtent.width, _screenExtent.height);
    _hiZMipLevels = static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;

    // Create Hi-Z texture with mip chain (1x sample)
    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R32_SFLOAT;
    imageCreateInfo.extent.width = _screenExtent.width;
    imageCreateInfo.extent.height = _screenExtent.height;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = _hiZMipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    shared_ptr<Image> image = make_shared<Image>(_device, imageCreateInfo, VK_IMAGE_ASPECT_COLOR_BIT);
    shared_ptr<Sampler> sampler = device.GetResourceCache().RequestSampler(DEFAULT_SAMPLER);
    _hiZTexture = make_shared<Texture>("HiZ", image, sampler);

    // Load shaders
    _depthResolveShader = device.GetResourceCache().RequestShader("Shaders/depthResolve.comp.spv");
    _depthResolvePipeline = make_unique<Pipeline>(device, *_depthResolveShader);

    _hiZGenerateShader = device.GetResourceCache().RequestShader("Shaders/hiZGenerate.comp.spv");
    _hiZPipeline = make_unique<Pipeline>(device, *_hiZGenerateShader);
}

void Core::RendererBatches::GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer, shared_ptr<Texture> depth)
{
    auto& hiZTextureImage = *_hiZTexture->GetImage().lock();
    auto& depthBufferImage = *depth->GetImage().lock();

    // Depth buffer: DEPTH_ATTACHMENT > SHADER_READ_ONLY
    commandBuffer.TransitionImageLayout(
        depthBufferImage,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Hi-Z texture: UNDEFINED > GENERAL
    commandBuffer.TransitionImageLayout(
        hiZTextureImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

	// Step 1: Resolve MSAA Depth > Hi-Z Mip 0
	// Use a unique key per depth image to avoid descriptor caching issues
	size_t resolveKey = _depthResolveShader->GetHash() ^ reinterpret_cast<size_t>(&depthBufferImage);
	auto& resolveResources = renderFrame.GetOrCreateShaderResources(resolveKey);

	TextureBuffer depthTex{};
	depthTex.texture = depth;
	depthTex.mipLevel = 0;
	depthTex.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	resolveResources.textureBuffers[0] = depthTex;

	TextureBuffer hiZTex{};
	hiZTex.texture = _hiZTexture;
	hiZTex.mipLevel = 0;
	hiZTex.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	resolveResources.textureBuffers[1] = hiZTex;

    // Step 1: Resolve MSAA Depth > Hi-Z Mip 0
    commandBuffer.BindPipeline(_depthResolvePipeline.get());

	if (resolveResources.isDescriptorSetUpdated == false)
	{
		renderFrame.AllocateDescriptorSetsWithKey(*_depthResolveShader, resolveKey);
		renderFrame.UpdateDescriptorSetsWithKey(*_depthResolveShader, resolveKey);
		resolveResources.isDescriptorSetUpdated = true;
	}

	commandBuffer.BindDescriptorSetsWithKey(renderFrame, VK_PIPELINE_BIND_POINT_COMPUTE,
		*_depthResolveShader, resolveKey);

    struct DepthResolvePushConstants {
        int32_t outputWidth;
        int32_t outputHeight;
        int32_t sampleCount;
        int32_t padding;
    } resolvePc = {
        static_cast<int32_t>(_screenExtent.width),
        static_cast<int32_t>(_screenExtent.height),
        static_cast<int32_t>(depthBufferImage.GetSampleCount()),
        0
    };

    commandBuffer.PushConstants(*_depthResolveShader, 0, &resolvePc);

    uint32_t groupX = (_screenExtent.width + 7) / 8;
    uint32_t groupY = (_screenExtent.height + 7) / 8;
    commandBuffer.Dispatch(groupX, groupY, 1);

	commandBuffer.TransitionImageLayout(hiZTextureImage,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_GENERAL);

    // Step 2: Generate Hi-Z mip chain
    if (_hiZMipLevels > 1)
    {
        commandBuffer.BindPipeline(_hiZPipeline.get());

        uint32_t mipWidth = _screenExtent.width;
        uint32_t mipHeight = _screenExtent.height;

        for (uint32_t mip = 1; mip < _hiZMipLevels; ++mip)
        {
            mipWidth = std::max(1u, mipWidth / 2);
            mipHeight = std::max(1u, mipHeight / 2);

            size_t uniqueKey = (_hiZGenerateShader->GetHash() << 8) | mip;
            auto& resources = renderFrame.GetOrCreateShaderResources(uniqueKey);

			//HACK HACK! Needs a new descriptor set system to avoid this kind of manual setup
            TextureBuffer srcTex{};
            srcTex.texture = _hiZTexture;
            srcTex.mipLevel = mip - 1;
            srcTex.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            resources.textureBuffers[0] = srcTex;

            TextureBuffer dstTex{};
            dstTex.texture = _hiZTexture;
            dstTex.mipLevel = mip;
            dstTex.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            resources.textureBuffers[1] = dstTex;

            renderFrame.AllocateDescriptorSetsWithKey(*_hiZGenerateShader, uniqueKey);
            renderFrame.UpdateDescriptorSetsWithKey(*_hiZGenerateShader, uniqueKey);
            commandBuffer.BindDescriptorSetsWithKey(renderFrame, VK_PIPELINE_BIND_POINT_COMPUTE,
                *_hiZGenerateShader, uniqueKey);

            struct HiZPushConstants {
                int32_t outputWidth;
                int32_t outputHeight;
            } hiZPc = { static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) };

            commandBuffer.PushConstants(*_hiZGenerateShader, 0, &hiZPc);

            groupX = (mipWidth + 7) / 8;
            groupY = (mipHeight + 7) / 8;
            commandBuffer.Dispatch(groupX, groupY, 1);
        }
    }

    // Hi-Z: GENERAL > SHADER_READ_ONLY
    commandBuffer.TransitionImageLayout(
        hiZTextureImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Depth buffer: SHADER_READ_ONLY > DEPTH_ATTACHMENT
    commandBuffer.TransitionImageLayout(depthBufferImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void Core::RendererBatches::DispatchFrustumOnlyCulling(RenderFrame& renderFrame,
	CommandBuffer& commandBuffer, const CameraBuffer& camera)
{
	uint32_t drawCount = _indirectDrawBuffer.GetDrawCount();

	// Reset instance counts
	commandBuffer.BindPipeline(_resetDrawCommandsSimplePipeline.get());

	renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsSimpleShader, 0, _indirectCommandBuffer);

	commandBuffer.PushConstants(*_resetDrawCommandsSimpleShader, 0, &drawCount);
	commandBuffer.BindDescriptorSets(renderFrame,
		_resetDrawCommandsSimplePipeline->GetPipelineBindPoint(), *_resetDrawCommandsSimpleShader);

	uint32_t groupCount = (drawCount + 63) / 64;
	commandBuffer.Dispatch(std::max(1u, groupCount), 1, 1);

	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	// Dispatch frustum-only culling
	struct FrustumCullData
	{
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 frustumPlanes[6];
		uint32_t drawCount;
	} cullData{};

	cullData.view = camera.View;
	cullData.proj = camera.Projection;
	cullData.drawCount = _instanceCount;

	glm::mat4 viewProj = camera.Projection * camera.View;
	ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

	commandBuffer.BindPipeline(_frustumCullingPipeline.get());

	renderFrame.SetShaderUniformBuffer(*_frustumCullingShader, 0, &cullData);
	renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 1, _objectDataBuffer);
	renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 2, _transformBatch.TransformBuffer);
	renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 3, _instanceBuffer);
	renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 4, _indirectCommandBuffer);

	commandBuffer.BindDescriptorSets(renderFrame,
		_frustumCullingPipeline->GetPipelineBindPoint(), *_frustumCullingShader);

	groupCount = (_instanceCount + 63) / 64;
	commandBuffer.Dispatch(groupCount, 1, 1);

	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
}
