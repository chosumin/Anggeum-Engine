#include "stdafx.h"
#include "Shader.h"
#include "Vertex.h"
#include "PushConstant.h"
#include "UniformBuffer.h"
#include "TextureBuffer.h"
#include "DescriptorPool.h"

Core::Shader::Shader(Device& device, const string& vertFilePath, const string& fragFilePath)
	:_device(device)
{
	auto vkDevice = _device.GetDevice();

	auto vertShaderCode = ReadFile(vertFilePath);
	auto fragShaderCode = ReadFile(fragFilePath);

	_vertShaderModule = CreateShaderModule(vkDevice, vertShaderCode);
	_fragShaderModule = CreateShaderModule(vkDevice, fragShaderCode);
}

Core::Shader::~Shader()
{
	auto vkDevice = _device.GetDevice();

	vkDestroyShaderModule(vkDevice, _fragShaderModule, nullptr);
	vkDestroyShaderModule(vkDevice, _vertShaderModule, nullptr);

	vkDestroyPipelineLayout(vkDevice, _pipelineLayout, nullptr);

	delete(_descriptorPool);
}

vector<VkPipelineShaderStageCreateInfo> Core::Shader::GetShaderStageCreateInfo() const
{
	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = _vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = _fragShaderModule;
	fragShaderStageInfo.pName = "main";

	vector<VkPipelineShaderStageCreateInfo> shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

	return shaderStages;
}

VkPipelineVertexInputStateCreateInfo Core::Shader::GetVertexInputStateCreateInfo()
{
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount =
		static_cast<uint32_t>(_vertexBindings.size());
	vertexInputInfo.vertexAttributeDescriptionCount =
		static_cast<uint32_t>(_vertexAttributes.size());
	vertexInputInfo.pVertexBindingDescriptions = _vertexBindings.data();
	vertexInputInfo.pVertexAttributeDescriptions = _vertexAttributes.data();

	return vertexInputInfo;
}

void Core::Shader::CreatePipelineLayout(vector<IDescriptor*> descriptors, vector<PushConstant> pushConstants)
{
	CreateDescriptorPool(descriptors);
	CreatePushConstants(pushConstants);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &_descriptorPool->GetDescriptorSetLayout();

	pipelineLayoutInfo.pushConstantRangeCount =
		static_cast<uint32_t>(_pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges = _pushConstantRanges.data();

	if (vkCreatePipelineLayout(_device.GetDevice(), &pipelineLayoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create pipeline layout!");
}

vector<char> Core::Shader::ReadFile(const string& filePath)
{
	ifstream file{ filePath, ios::ate | ios::binary };

	if (!file.is_open())
		throw runtime_error{ "failed to open file" };

	size_t fileSize{ static_cast<size_t>(file.tellg()) };
	vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

VkShaderModule Core::Shader::CreateShaderModule(VkDevice& device, const vector<char>& code) const
{
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create shader module!");
	}

	return shaderModule;
}

VkShaderStageFlags Core::Shader::GetPushConstantsShaderStage() const
{
	VkShaderStageFlags flags{};

	for (auto& pushConstantRange : _pushConstantRanges)
	{
		flags |= pushConstantRange.stageFlags;
	}

	return flags;
}

VkDescriptorSetLayout& Core::Shader::GetDescriptorSetLayout()
{
	return _descriptorPool->GetDescriptorSetLayout();
}

VkDescriptorPool& Core::Shader::GetDescriptorPool()
{
	return _descriptorPool->AllocateDescriptorPool();
}

void Core::Shader::CreateDescriptorPool(vector<IDescriptor*> descriptors)
{
	_descriptorPool = new DescriptorPool(_device, descriptors);
}

void Core::Shader::CreatePushConstants(vector<PushConstant>& pushConstants)
{
	uint32_t offset = 0;

	for (auto& pushConstant : pushConstants)
	{
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.offset = offset;
		pushConstantRange.stageFlags = pushConstant.StageFlags;
		pushConstantRange.size += pushConstant.Size;

		offset += pushConstant.Size;

		_pushConstantRanges.emplace_back(pushConstantRange);
	}
}