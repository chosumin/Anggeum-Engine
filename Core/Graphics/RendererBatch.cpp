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
#include "TransferJob.h"

using namespace Core;

Core::RendererBatches::RendererBatches(Device& device, TransformBatch& transformBatch)
	: _device(device)
	, _transformBatch(transformBatch)
	, _instanceBuffer(VK_NULL_HANDLE)
{
}

Core::RendererBatches::~RendererBatches()
{
	if (_materialIndexBuffer != VK_NULL_HANDLE)
		delete(_materialIndexBuffer);

	if (_indirectCommandBuffer != VK_NULL_HANDLE)
		delete(_indirectCommandBuffer);

	if (_instanceBuffer != VK_NULL_HANDLE)
		delete(_instanceBuffer);

	if (_objectDataBuffer != VK_NULL_HANDLE)
		delete(_objectDataBuffer);

	if (_visibleCountsBuffer != VK_NULL_HANDLE)
		delete(_visibleCountsBuffer);

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
			auto& shader = materials[i]->GetShader();

			if (shader.GetPass() == "Geometry")
			{
				auto& material = materials[i];
				auto& subMesh = mesh->GetSubMeshes()[i];

				AddBatch(device, renderPass, pipelineState, entityId, material, subMesh);
			}
		}
	}

	CreateInstanceBuffer(device);
}

void Core::RendererBatches::PrepareGPUDrivenRendering(Device& device, bool needMaterialData, 
	shared_ptr<Texture> depthBuffer)
{
	_needsMaterialIndexBuffer = needMaterialData;

	_indirectDrawBuffer.Clear();
	uint32_t globalFirstInstance = 0;

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
					instanceCount,
					globalFirstInstance
				);
				globalFirstInstance += instanceCount;

				//Prepare object data for each instance
				for (size_t i = 0; i < subMeshBatch.Transforms.size(); ++i)
				{
					GPUObjectData data{};
					data.boundingSphere = glm::vec4(
						allocation.boundingSphereCenter,
						allocation.boundingSphereRadius);
					data.transformIndex = subMeshBatch.Transforms[i];

					objectData.push_back(data);
				}
			}
		}
	}

	vector<Job*> jobs;

	// Indirect Command Buffer
	Core::VkBufferJob<DrawIndexedIndirectCommand> job(device,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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

	PrepareHiZResources(device, depthBuffer);
	PrepareCullingResources(device);
}

void Core::RendererBatches::PrepareCullingResources(Core::Device& device)
{
	_cullingShader = device.GetResourceCache().RequestShader("Shaders/gpuCulling.comp");
	_cullingPipeline = make_unique<Pipeline>(device, *_cullingShader);

	_visibleCountsBuffer = new Core::Buffer(device,
		sizeof(uint32_t),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		MemoryType::DEVICE_LOCAL);
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

        //1. Material batch
        for (auto&& materialBatch : shaderBatch.second.MaterialBatches)
		{
			auto sharedMaterial = materialBatch.second.Material.lock();

			commandBuffer.BindDescriptorSets(
				renderFrame,
				VK_PIPELINE_BIND_POINT_GRAPHICS, *sharedMaterial);

			//2. SubMesh batch
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
			renderFrame.SetShaderUniformBuffer(*shader, 7, const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
			renderFrame.SetShaderStorageBuffer(*shader, 8, _materialIndexBuffer);
		}
		
		if (perShader)
			perShader(shader);

		commandBuffer.BindDescriptorSets(renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS, *shader);

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
		//Add new shader batch
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
		//Add new material batch
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

void Core::RendererBatches::PrepareHiZResources(Device& device, shared_ptr<Texture> depthBuffer)
{
    if (depthBuffer == nullptr)
        return;

    _previousDepthBuffer = depthBuffer;
    _screenExtent = _previousDepthBuffer->GetExtent();

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
    _depthResolveShader = device.GetResourceCache().RequestShader("Shaders/depthResolve.comp");
    _depthResolvePipeline = make_unique<Pipeline>(device, *_depthResolveShader);

    _hiZGenerateShader = device.GetResourceCache().RequestShader("Shaders/hiZGenerate.comp");
    _hiZPipeline = make_unique<Pipeline>(device, *_hiZGenerateShader);
}

void Core::RendererBatches::GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
    if (!_previousDepthBuffer || !_hiZTexture)
        return;

    auto& hiZTextureImage = *_hiZTexture->GetImage().lock();
    auto& depthBufferImage = *_previousDepthBuffer->GetImage().lock();

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
    commandBuffer.BindPipeline(_depthResolvePipeline.get());

    renderFrame.SetShaderTextureBuffer(*_depthResolveShader, 0, _previousDepthBuffer, 0, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    renderFrame.SetShaderTextureBuffer(*_depthResolveShader, 1, _hiZTexture, 0, 
        VK_IMAGE_LAYOUT_GENERAL);

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
    commandBuffer.BindDescriptorSets(renderFrame, VK_PIPELINE_BIND_POINT_COMPUTE, *_depthResolveShader);

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

void Core::RendererBatches::DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer, const CameraBuffer& camera)
{
	// Generate Hi-Z buffer from previous frame's depth
	if (_previousDepthBuffer)
	{
		GenerateHiZBuffer(renderFrame, commandBuffer);
	}

	GPUCullData cullData{};
	cullData.view = camera.View;
	cullData.proj = camera.Projection;
	cullData.screenSize = glm::vec2(_screenExtent.width, _screenExtent.height);
	cullData.drawCount = _instanceCount;
	cullData.hiZMipLevels = _hiZMipLevels;
	cullData.enableOcclusionCulling = _previousDepthBuffer ? 1 : 0;

	glm::mat4 viewProj = camera.Projection * camera.View;
	ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

	// Reset visible counts to 0
	commandBuffer.FillBuffer(*_visibleCountsBuffer, 0, sizeof(uint32_t), 0);

	commandBuffer.BufferBarrier(
		*_visibleCountsBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	commandBuffer.BindPipeline(_cullingPipeline.get());

	renderFrame.SetShaderUniformBuffer(*_cullingShader, 0, &cullData);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 1, _objectDataBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 2, _transformBatch.TransformBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 3, _instanceBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 4, _indirectCommandBuffer);
	renderFrame.SetShaderStorageBuffer(*_cullingShader, 5, _visibleCountsBuffer);
	
	// Hi-Z texture binding
	if (_hiZTexture)
	{
		renderFrame.SetShaderTextureBuffer(*_cullingShader, 6, _hiZTexture);
	}

	commandBuffer.BindDescriptorSets(
		renderFrame,
		_cullingPipeline->GetPipelineBindPoint(),
		*_cullingShader);

	const uint32_t WORKGROUP_SIZE = 64;
	uint32_t groupCount = (_instanceCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
	commandBuffer.Dispatch(groupCount, 1, 1);

	//Barrier for instance buffer and indirect draw buffer
	commandBuffer.Barrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
}