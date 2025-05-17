#include "stdafx.h"
#include "Shader.h"
#include "DescriptorPool.h"
#include "Graphics/SpirvUtility.h"
#include "Utils/FileSystem.h"
#include "Utils/Utility.h"
#include "spirv_cross/spirv_cross.hpp"

Core::Shader::Shader(Device& device, const string pass,
	const string& vertFilePath,
	const string& fragFilePath)
	:_device(device), _pass(pass)
{
	auto vkDevice = _device.GetDevice();

	vector<uint32_t> vertShaderCode;
	vector<uint32_t> fragShaderCode;

	if (FileSystem::GetExtension(vertFilePath) != "spv")
	{
		vertShaderCode = FileSystem::Read32("Assets/" + vertFilePath);
		const char* vert = reinterpret_cast<const char*>(vertShaderCode.data());
		vertShaderCode = SpirvUtility::GLSLToSPV(VK_SHADER_STAGE_VERTEX_BIT, vert, vertFilePath);
	}
	else
	{
		vertShaderCode = FileSystem::Read32Fast("Assets/" + vertFilePath);
	}

	size_t vertCodeSize = vertShaderCode.size() * sizeof(uint32_t);

	if (FileSystem::GetExtension(fragFilePath) != "spv")
	{
		fragShaderCode = FileSystem::Read32("Assets/" + fragFilePath);
		const char* frag = reinterpret_cast<const char*>(fragShaderCode.data());
		fragShaderCode = SpirvUtility::GLSLToSPV(VK_SHADER_STAGE_FRAGMENT_BIT, frag, fragFilePath);
	}
	else
	{
		fragShaderCode = FileSystem::Read32Fast("Assets/" + fragFilePath);
	}

	size_t fragCodeSize = fragShaderCode.size() * sizeof(uint32_t);
	
	SetResources(vertFilePath, vertShaderCode,
		fragFilePath, fragShaderCode);

	_vertShaderModule = CreateShaderModule(vkDevice, vertShaderCode, vertCodeSize);
	_fragShaderModule = CreateShaderModule(vkDevice, fragShaderCode, fragCodeSize);

	_hash = Utility::HashCode((vertFilePath + fragFilePath).c_str());
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
	_textureBufferLayoutBindings(std::move(other._textureBufferLayoutBindings)),
	_pass(other._pass),
	_hash(other._hash)
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
	   _pass = other._pass;
	   _hash = other._hash;

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
	bool isNew = true;
	for (auto&& layoutBinding : _uniformBufferLayoutBindings)
	{
		if (layoutBinding.Binding == binding)
		{
			layoutBinding.Stage = 
				static_cast<VkShaderStageFlagBits>(layoutBinding.Stage | stage);
			isNew = false;
			break;
		}
	}

	if (isNew)
		_uniformBufferLayoutBindings.emplace_back(binding, stage, size);
}

void Core::Shader::AddTextureBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage)
{
	bool isNew = true;
	for (auto&& layoutBinding : _textureBufferLayoutBindings)
	{
		if (layoutBinding.Binding == binding)
		{
			layoutBinding.Stage =
				static_cast<VkShaderStageFlagBits>(layoutBinding.Stage | stage);
			isNew = false;
			break;
		}
	}

	if (isNew)
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

VkShaderModule Core::Shader::CreateShaderModule(VkDevice& device, const vector<uint32_t>& code, size_t codeSize) const
{
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = codeSize;
	createInfo.pCode = code.data();

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

void Core::Shader::SetResources(const string vertPath, const vector<uint32_t>& vertSpirvBinary, const string fragPath, const vector<uint32_t>& fragSpirvBinary)
{
	SpirvUtility::SetResources(*this, VK_SHADER_STAGE_VERTEX_BIT, 
		vertPath, vertSpirvBinary);
	SpirvUtility::SetResources(*this, VK_SHADER_STAGE_FRAGMENT_BIT, 
		fragPath, fragSpirvBinary);
}
