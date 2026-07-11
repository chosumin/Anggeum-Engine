#include "stdafx.h"
#include "LightCullingPass.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Foundation/Scene.h"

Core::LightCullingPass::LightCullingPass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene, VkExtent2D swapChainExtents, ivec2 tileNums, 
	Buffer* lightVisibilityBuffer)
	:RendererPass(device, workerThreadManager), _scene(scene),
	_lightVisibilityBuffer(lightVisibilityBuffer)
{
	_computeMaterial = device.GetResourceCache().RequestMaterial("lightCulling", "shaders/lightCulling.comp.spv");
	_computePipeline = make_unique<Core::Pipeline>(device, _computeMaterial->GetShader());

	_tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
	_tileInfo.tileNums = tileNums;
}

Core::LightCullingPass::~LightCullingPass()
{
}

void Core::LightCullingPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	auto depthTarget = renderFrame.GetCurrentDepth();
	if (!depthTarget)
		return;

	UpdateLightBuffer();

	auto& commandBuffer = renderFrame.GetCommandBuffer();

	PerspectiveCamera* camera = _scene.GetMainCamera();

	auto builder = renderFrame.CreateDescriptorSetBuilder(_computeMaterial->GetShader(), 0);
	builder.SetUniformBuffer(0, &camera->Matrices);
	builder.SetStorageBuffer(1, _lightVisibilityBuffer);
	builder.SetTextureBuffer(2, depthTarget);
	builder.SetUniformBuffer(3, &_lightBuffer);
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(_computePipeline.get());

	commandBuffer.BindDescriptorSet(
		_computePipeline->GetPipelineBindPoint(),
		_computeMaterial->GetShader(), resources);

	_computeMaterial->SetPushConstants<TileInfo>(_tileInfo);
	commandBuffer.PushConstants(*_computeMaterial, 0);

	commandBuffer.Dispatch(_tileInfo.tileNums.x, _tileInfo.tileNums.y, 1);
}

void Core::LightCullingPass::UpdateLightBuffer()
{
	auto lights = _scene.GetComponents<Light>();

	uint32_t size = std::min((uint32_t)lights.size(), (uint32_t)MAX_FORWARD_LIGHT_COUNT);
	for (uint32_t i = 0; i < size; ++i)
	{
		auto light = lights[i];

		auto& properties = light->GetProperties();
		auto& transform = light->GetEntity().GetTransform();

		LightInfo lightInfo{};
		lightInfo.Position = vec4(transform.GetTranslation(),
			static_cast<float>(light->GetLightType()));
		lightInfo.Color = vec4(properties.Color, properties.Intensity);

		auto direction = transform.GetRotation() * properties.Direction;
		lightInfo.Direction =
			vec4(direction, properties.Range);
		lightInfo.Info = vec2(properties.InnerConeAngle, properties.OuterConeAngle);

		_lightBuffer.Light[i] = lightInfo;
	}

	_lightBuffer.Count = size;
}
