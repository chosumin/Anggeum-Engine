#include "stdafx.h"
#include "SampleShader.h"
#include "CameraUniformBuffer.h"
#include "TextureBuffer.h"
#include "Mesh.h"
#include "Texture.h"
#include "CommandBuffer.h"

namespace Core
{
	SampleShader::SampleShader(Device& device, CommandBuffer& commandBuffer)
		:Shader(device,
			"shaders/simple_vs.vert.spv",
			"shaders/simple_fs.frag.spv")
	{
		auto cameraBuffer = 
			new UniformBuffer(sizeof(VPBufferObject), 0);
		_uniformBuffers.insert({ 0, cameraBuffer });

		auto textureBuffer = new TextureBuffer(1);
		_textureBuffers.insert({ 1, textureBuffer });

		_texture = new Core::Texture(commandBuffer.GetCommandPool(),
			"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha, 1);
		SetBuffer(1, _texture->GetDescriptorImageInfo());

		vector<PushConstant> pushConstants = 
		{ 
			{sizeof(ModelWorld), VK_SHADER_STAGE_VERTEX_BIT} 
		};

		CreatePipelineLayout({ cameraBuffer, textureBuffer }, pushConstants);
	}

	SampleShader::~SampleShader()
	{
		delete(_texture);
	}

	type_index SampleShader::GetType()
	{
		return typeid(SampleShader);
	}
}
