#include "stdafx.h"
#include "Shader.h"
#include "Vertex.h"
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

Core::Shader::Shader(Shader&& other) noexcept
    : _device(other._device),
      _vertShaderModule(other._vertShaderModule),
      _fragShaderModule(other._fragShaderModule),
      _pipelineLayout(other._pipelineLayout),
      _descriptorPool(other._descriptorPool),
      _vertexBindings(std::move(other._vertexBindings)),
      _vertexAttributes(std::move(other._vertexAttributes)),
      _pushConstantRanges(std::move(other._pushConstantRanges)),
      _uniformBufferLayoutBindings(std::move(other._uniformBufferLayoutBindings)),
      _textureBufferLayoutBindings(std::move(other._textureBufferLayoutBindings))
{
    other._vertShaderModule = VK_NULL_HANDLE;
    other._fragShaderModule = VK_NULL_HANDLE;
    other._pipelineLayout = VK_NULL_HANDLE;
    other._descriptorPool = nullptr;
}

Core::Shader& Core::Shader::operator=(Shader&& other) noexcept  
{  
   if (this != &other)  
   {  
       auto vkDevice = _device.GetDevice();  

       // Clean up existing resources  
       vkDestroyShaderModule(vkDevice, _fragShaderModule, nullptr);  
       vkDestroyShaderModule(vkDevice, _vertShaderModule, nullptr);  
       vkDestroyPipelineLayout(vkDevice, _pipelineLayout, nullptr);  
       delete _descriptorPool;  

       // Move resources from the other object
       _vertShaderModule = other._vertShaderModule;  
       _fragShaderModule = other._fragShaderModule;  
       _pipelineLayout = other._pipelineLayout;  
       _descriptorPool = other._descriptorPool;  
       _vertexBindings = std::move(other._vertexBindings);  
       _vertexAttributes = std::move(other._vertexAttributes);  
       _pushConstantRanges = std::move(other._pushConstantRanges);  
       _uniformBufferLayoutBindings = std::move(other._uniformBufferLayoutBindings);  
       _textureBufferLayoutBindings = std::move(other._textureBufferLayoutBindings);  

       // Reset the other object  
       other._vertShaderModule = VK_NULL_HANDLE;  
       other._fragShaderModule = VK_NULL_HANDLE;  
       other._pipelineLayout = VK_NULL_HANDLE;  
       other._descriptorPool = nullptr;  
   }  

   return *this;  
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

void Core::Shader::CreatePipelineLayout()
{
	CreateDescriptorPool();

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

void Core::Shader::AddUniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage, VkDeviceSize size)
{
	_uniformBufferLayoutBindings.emplace_back(binding, stage, size);
}

void Core::Shader::AddTextureBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage)
{
	_textureBufferLayoutBindings.emplace_back(binding, stage);
}

void Core::Shader::AddPushConstantsRange(VkShaderStageFlags stage, uint32_t size)
{
	uint32 offset = 0;
	for (auto&& range : _pushConstantRanges)
	{
		offset += range.size;
	}

	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = stage;
	pushConstant.size = size;
	pushConstant.offset = offset;
	_pushConstantRanges.push_back(pushConstant);
}

vector<char> Core::Shader::ReadFile(const string& filePath)
{
	ifstream file{ "Assets/" + filePath, ios::ate | ios::binary};

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

VkShaderStageFlags Core::Shader::GetPushConstantsShaderStage(uint32_t index) const
{
	if (index >= _pushConstantRanges.size())
	{
		throw std::out_of_range("Push constant index out of range");
	}

	return _pushConstantRanges[index].stageFlags;
}

uint32_t Core::Shader::GetPushConstantsOffset(uint32_t index) const
{
	if (index >= _pushConstantRanges.size())
	{
		throw std::out_of_range("Push constant index out of range");
	}

	return _pushConstantRanges[index].offset;
}



VkDescriptorSetLayout& Core::Shader::GetDescriptorSetLayout()
{
	return _descriptorPool->GetDescriptorSetLayout();
}

VkDescriptorPool& Core::Shader::GetDescriptorPool()
{
	return _descriptorPool->AllocateDescriptorPool();
}

void Core::Shader::CreateDescriptorPool()
{
	vector<IDescriptor*> descriptors;
	
	for (auto& binding : _uniformBufferLayoutBindings)
	{
		descriptors.push_back(&binding);
	}

	for (auto& binding : _textureBufferLayoutBindings)
	{
		descriptors.push_back(&binding);
	}

	_descriptorPool = new DescriptorPool(_device, descriptors);
}