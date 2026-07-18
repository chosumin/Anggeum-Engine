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
	:_device(device), _pass(pass), _computeShaderModule(VK_NULL_HANDLE)
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

Core::Shader::Shader(Device& device, const string pass, const string& computeFilePath)
	:_device(device), _pass(pass), _vertShaderModule(VK_NULL_HANDLE), _fragShaderModule(VK_NULL_HANDLE)
{
	auto vkDevice = _device.GetDevice();

	vector<uint32_t> computeShaderCode;

	if (FileSystem::GetExtension(computeFilePath) != "spv")
	{
		computeShaderCode = FileSystem::Read32("Assets/" + computeFilePath);
		const char* compute = reinterpret_cast<const char*>(computeShaderCode.data());
		computeShaderCode = SpirvUtility::GLSLToSPV(VK_SHADER_STAGE_COMPUTE_BIT, compute, computeFilePath);
	}
	else
	{
		computeShaderCode = FileSystem::Read32Fast("Assets/" + computeFilePath);
	}

	size_t codeSize = computeShaderCode.size() * sizeof(uint32_t);

	SpirvUtility::SetResources(*this, VK_SHADER_STAGE_COMPUTE_BIT,
		computeFilePath, computeShaderCode);

	_computeShaderModule = CreateShaderModule(vkDevice, computeShaderCode, codeSize);

	_hash = Utility::HashCode(computeFilePath.c_str());
}

void Core::Shader::CreatePipelineLayout()
{
	vector<VkDescriptorSetLayout> layouts;

	// Collect layouts in order (Set 0, Set 1, Set 2)
	uint32_t maxSetIndex = 0;
	for (auto& [setIndex, layout] : _descriptorSetLayouts)
	{
		layout->Finalize();

		if (setIndex > maxSetIndex)
			maxSetIndex = setIndex;
	}

	// Add bindless set index if used
	if (_usesBindlessTextures)
	{
		uint32_t bindlessSetIndex = static_cast<uint32_t>(DescriptorSetType::Bindless);
		if (bindlessSetIndex > maxSetIndex)
			maxSetIndex = bindlessSetIndex;
	}

	// Resize to accommodate all sets (including gaps)
	if (_descriptorSetLayouts.empty() == false)
		layouts.resize(maxSetIndex + 1, VK_NULL_HANDLE);

	// Fill in owned layouts (Set 0, Set 1)
	for (auto& [setIndex, layout] : _descriptorSetLayouts)
	{
		layouts[setIndex] = layout->GetDescriptorSetLayout();
	}

	for (uint32_t i = 0; i < maxSetIndex; ++i)
	{
		if (layouts[i] == VK_NULL_HANDLE && i != static_cast<uint32_t>(DescriptorSetType::Bindless))
		{
			// Create empty descriptor set layout if any null layout found
			auto* emptyLayout = new DescriptorSetLayout(_device);
			emptyLayout->Finalize();
			_descriptorSetLayouts[i] = emptyLayout;
			layouts[i] = emptyLayout->GetDescriptorSetLayout();
		}
	}

	// Fill in bindless layout (Set 2, not owned)
	if (_usesBindlessTextures && _bindlessDescriptorSetLayout != VK_NULL_HANDLE)
	{
		layouts[static_cast<uint32_t>(DescriptorSetType::Bindless)] = _bindlessDescriptorSetLayout;
	}

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
	pipelineLayoutInfo.pSetLayouts = layouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(_pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges = _pushConstantRanges.data();

	if (vkCreatePipelineLayout(_device.GetDevice(), &pipelineLayoutInfo,
		nullptr, &_pipelineLayout) != VK_SUCCESS)
	{
		throw runtime_error("Failed to create pipeline layout!");
	}
}

// Move constructor
Core::Shader::Shader(Shader&& other) noexcept
	: _device(other._device),
	_vertShaderModule(other._vertShaderModule),
	_fragShaderModule(other._fragShaderModule),
	_computeShaderModule(other._computeShaderModule),
	_pipelineLayout(other._pipelineLayout),
	_descriptorSetLayouts(std::move(other._descriptorSetLayouts)),
	_vertexBindings(std::move(other._vertexBindings)),
	_vertexAttributes(std::move(other._vertexAttributes)),
	_pushConstantRanges(std::move(other._pushConstantRanges)),
	_pass(other._pass),
	_hash(other._hash),
	_vertexAttributeNames(std::move(other._vertexAttributeNames))
{
	other._vertShaderModule = VK_NULL_HANDLE;
	other._fragShaderModule = VK_NULL_HANDLE;
	other._computeShaderModule = VK_NULL_HANDLE;
	other._pipelineLayout = VK_NULL_HANDLE;
}

// Move assignment operator
Core::Shader& Core::Shader::operator=(Shader&& other) noexcept
{
	if (this != &other)
	{
		auto vkDevice = _device.GetDevice();

		// Clean up existing resources
		if (_fragShaderModule)
			vkDestroyShaderModule(vkDevice, _fragShaderModule, nullptr);
		if (_vertShaderModule)
			vkDestroyShaderModule(vkDevice, _vertShaderModule, nullptr);
		if (_computeShaderModule)
			vkDestroyShaderModule(vkDevice, _computeShaderModule, nullptr);
		vkDestroyPipelineLayout(vkDevice, _pipelineLayout, nullptr);
		
		for (auto& [setIndex, layout] : _descriptorSetLayouts)
			delete layout;

		// Move resources
		_vertShaderModule = other._vertShaderModule;
		_fragShaderModule = other._fragShaderModule;
		_computeShaderModule = other._computeShaderModule;
		_pipelineLayout = other._pipelineLayout;
		_descriptorSetLayouts = std::move(other._descriptorSetLayouts);
		_vertexBindings = std::move(other._vertexBindings);
		_vertexAttributes = std::move(other._vertexAttributes);
		_pushConstantRanges = std::move(other._pushConstantRanges);
		_pass = other._pass;
		_hash = other._hash;
		_vertexAttributeNames = std::move(other._vertexAttributeNames);

		// Reset other object
		other._vertShaderModule = VK_NULL_HANDLE;
		other._fragShaderModule = VK_NULL_HANDLE;
		other._computeShaderModule = VK_NULL_HANDLE;
		other._pipelineLayout = VK_NULL_HANDLE;
	}

	return *this;
}

Core::Shader::~Shader()
{
	auto vkDevice = _device.GetDevice();

	if (_fragShaderModule)
		vkDestroyShaderModule(vkDevice, _fragShaderModule, nullptr);
	if (_vertShaderModule)
		vkDestroyShaderModule(vkDevice, _vertShaderModule, nullptr);
	if (_computeShaderModule)
		vkDestroyShaderModule(vkDevice, _computeShaderModule, nullptr);

	vkDestroyPipelineLayout(vkDevice, _pipelineLayout, nullptr);

	// Delete all descriptor set layouts
	for (auto& [setIndex, layout] : _descriptorSetLayouts)
	{
		delete layout;
	}
	_descriptorSetLayouts.clear();
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

VkPipelineShaderStageCreateInfo Core::Shader::GetComputeShaderStageCreateInfo() const
{
	VkPipelineShaderStageCreateInfo shaderStageInfo{};
	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStageInfo.module = _computeShaderModule;
	shaderStageInfo.pName = "main";

	return shaderStageInfo;
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

void Core::Shader::AddUniformBufferLayoutBinding(uint32_t set, uint32_t binding, VkShaderStageFlags stage, VkDeviceSize size)
{
	auto* layout = GetOrCreateDescriptorSetLayout(set);
	layout->AddUniformBufferBinding(binding, stage, size);
}

void Core::Shader::AddTextureBufferLayoutBinding(uint32_t set, uint32_t binding, VkShaderStageFlags stage,
	VkDescriptorType descriptorType)
{
	auto* layout = GetOrCreateDescriptorSetLayout(set);
	layout->AddTextureBufferBinding(binding, stage, descriptorType);
}

void Core::Shader::AddStorageBufferLayoutBinding(uint32_t set, uint32_t binding, VkShaderStageFlags stage)
{
	auto* layout = GetOrCreateDescriptorSetLayout(set);
	layout->AddStorageBufferBinding(binding, stage);
}

Core::DescriptorSetLayout* Core::Shader::GetOrCreateDescriptorSetLayout(uint32_t setIndex)
{
	auto it = _descriptorSetLayouts.find(setIndex);
	if (it != _descriptorSetLayouts.end())
	{
		return it->second;
	}
	
	// Create new DescriptorSetLayout if not exists
	auto* newLayout = new DescriptorSetLayout(_device);
	_descriptorSetLayouts[setIndex] = newLayout;
	return newLayout;
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

void Core::Shader::SetResources(const string vertPath, const vector<uint32_t>& vertSpirvBinary, const string fragPath, const vector<uint32_t>& fragSpirvBinary)
{
	SpirvUtility::SetResources(*this, VK_SHADER_STAGE_VERTEX_BIT, 
		vertPath, vertSpirvBinary);
	SpirvUtility::SetResources(*this, VK_SHADER_STAGE_FRAGMENT_BIT, 
		fragPath, fragSpirvBinary);
}

void Core::Shader::AddPushConstantsRange(VkShaderStageFlags stage, uint32_t offset, uint32_t size)
{
	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = stage;
	pushConstant.size = size;
	pushConstant.offset = offset;
	_pushConstantRanges.push_back(pushConstant);
}