#include "stdafx.h"
#include "Shader.h"
#include "Vertex.h"
#include "PushConstant.h"
#include "UniformBuffer.h"
#include "TextureBuffer.h"

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

	vkDestroyDescriptorPool(vkDevice, _descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(vkDevice, _descriptorSetLayout, nullptr);
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
	_vertexBinding = Vertex::GetBindingDescription();
	_vertexAttribute = Vertex::GetAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(_vertexAttribute.size());
	vertexInputInfo.pVertexBindingDescriptions = &_vertexBinding;
	vertexInputInfo.pVertexAttributeDescriptions = _vertexAttribute.data();

	return vertexInputInfo;
}

void Core::Shader::CreatePipelineLayout(vector<IDescriptor*> descriptors, vector<PushConstant> pushConstants)
{
	CreateDescriptors(descriptors);
	CreatePushConstants(pushConstants);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;

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

void Core::Shader::CreateDescriptors(vector<IDescriptor*> descriptors)
{
	CreateDescriptorSetLayout(descriptors);
	CreateDescriptorPool(descriptors);
	CreateDescriptorSets(descriptors);
}

void Core::Shader::CreateDescriptorSetLayout(vector<IDescriptor*> descriptors)
{
	size_t size = descriptors.size();

	vector<VkDescriptorSetLayoutBinding> bindings(size);
	for (size_t i = 0; i < size; i++)
	{
		bindings[i] = (descriptors[i]->CreateDescriptorSetLayoutBinding());
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(
		Device::Instance().GetDevice(),
		&layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
	{
		throw runtime_error("failed to create descriptor set layout!");
	}
}

void Core::Shader::CreateDescriptorPool(vector<IDescriptor*> descriptors)
{
	size_t size = descriptors.size();

	vector<VkDescriptorPoolSize> poolSizes(size);
	for (size_t i = 0; i < size; i++)
	{
		poolSizes[i].type = descriptors[i]->GetDescriptorType();
		poolSizes[i].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	}

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

	if (vkCreateDescriptorPool(Device::Instance().GetDevice(),
		&poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
	{
		throw runtime_error("failed to create descriptor pool!");
	}
}

void Core::Shader::CreateDescriptorSets(vector<IDescriptor*> descriptors)
{
	vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, _descriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	allocInfo.pSetLayouts = layouts.data();

	_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
	if (vkAllocateDescriptorSets(Device::Instance().GetDevice(),
		&allocInfo, _descriptorSets.data()) != VK_SUCCESS)
	{
		throw runtime_error("failed to allocate descriptor sets!");
	}
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