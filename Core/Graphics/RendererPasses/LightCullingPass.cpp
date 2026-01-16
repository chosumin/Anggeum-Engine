#include "stdafx.h"
#include "LightCullingPass.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Foundation/Scene.h"

Core::LightCullingPass::LightCullingPass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene, VkExtent2D swapChainExtents, ivec2 tileNums, 
	shared_ptr<Texture> depthPrepassRenderTarget,
	Buffer* lightVisibilityBuffer)
	:RendererPass(device, workerThreadManager), _scene(scene),
	_lightVisibilityBuffer(lightVisibilityBuffer),
	_depthPrepassRenderTarget(depthPrepassRenderTarget)
{
	_computeMaterial = device.GetResourceCache().RequestMaterial("lightCulling", "shaders/lightCulling.comp");
	_computePipeline = make_unique<Core::Pipeline>(device, _computeMaterial->GetShader());

	_tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
	_tileInfo.tileNums = tileNums;
}

Core::LightCullingPass::~LightCullingPass()
{
}

void Core::LightCullingPass::Prepare()
{
	_computeMaterial->SetStorageBuffer(1, 1, _lightVisibilityBuffer);
	_computeMaterial->SetBuffer(1, 2, _depthPrepassRenderTarget);
}

void Core::LightCullingPass::Draw(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	UpdateLightBuffer();

	commandBuffer.TransitionImageLayout(*_depthPrepassRenderTarget->GetImage().lock(),
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	PerspectiveCamera* camera = _scene.GetMainCamera();
	_computeMaterial->SetBuffer(0, currentFrame, 0, &camera->Matrices);

	_computeMaterial->SetBuffer(1, currentFrame, 3, &_lightBuffer);

	commandBuffer.BindPipeline(_computePipeline.get());

	commandBuffer.BindDescriptorSets(
		_computePipeline->GetPipelineBindPoint(), *_computeMaterial, currentFrame);

	_computeMaterial->SetPushConstants<TileInfo>(_tileInfo);
	commandBuffer.PushConstants(*_computeMaterial, 0);

	commandBuffer.Dispatch(_tileInfo.tileNums.x, _tileInfo.tileNums.y, 1);

	commandBuffer.TransitionImageLayout(*_depthPrepassRenderTarget->GetImage().lock(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
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
