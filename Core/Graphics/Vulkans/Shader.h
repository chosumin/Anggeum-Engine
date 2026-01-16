#pragma once
#include "UniformBuffer.h"
#include "TextureBuffer.h"
#include "StorageBuffer.h"
#include "Graphics/Vulkans/Vertex.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class IDescriptor;
	class DescriptorSetLayout;
	
	class Shader
	{
	private:
		friend class SpirvUtility;
	public:
		Shader(Device& device, 
			const string pass,
			const string& vertFilePath,
			const string& fragFilePath);
		Shader(Device& device,
			const string pass,
			const string& computeFilePath);
		Shader(Shader&& other) noexcept;
		Shader& operator=(Shader&& other) noexcept;

		virtual ~Shader();

		uint32_t GetType() { return _hash; }
		
		string& GetPass() { return _pass; }

		bool UseInstancing() { return true; }
		vector<string> GetVertexAttirbuteNames() const
		{
			return _vertexAttributeNames;
		}

		uint32_t GetHash() const { return _hash; }

		void CreatePipelineLayout();

		VkPipelineVertexInputStateCreateInfo GetVertexInputStateCreateInfo();

		vector<VkPipelineShaderStageCreateInfo> GetShaderStageCreateInfo() const;
		VkPipelineShaderStageCreateInfo GetComputeShaderStageCreateInfo() const;

		VkShaderStageFlags GetPushConstantsShaderStage(uint32_t index) const;
		uint32_t GetPushConstantsOffset(uint32_t index) const;
		vector<VkPushConstantRange>& GetPushConstantRanges() { return _pushConstantRanges; }
		
		// Get descriptor set layouts as a map (set index -> DescriptorSetLayout*)
		const unordered_map<uint32_t, DescriptorSetLayout*>& GetDescriptorSetLayouts() const 
		{ 
			return _descriptorSetLayouts; 
		}
		
		VkPipelineLayout GetPipelineLayout() { return _pipelineLayout; }
		
	protected:
		// Add binding with set index
		void AddUniformBufferLayoutBinding(uint32_t set, uint32_t binding, VkShaderStageFlags stage, VkDeviceSize size);
		void AddTextureBufferLayoutBinding(uint32_t set, uint32_t binding, VkShaderStageFlags stage);
		void AddStorageBufferLayoutBinding(uint32_t set, uint32_t binding, VkShaderStageFlags stage);
		void AddPushConstantsRange(VkShaderStageFlags stage, uint32_t size);
		
	private:
		void SetResources(const string vertPath, const vector<uint32_t>& vertSpirvBinary,
			const string fragPath, const vector<uint32_t>& fragSpirvBinary);

		VkShaderModule CreateShaderModule(VkDevice& device, const vector<uint32_t>& code, size_t codeSize) const;
		
		// Get or create DescriptorSetLayout for the given set index
		DescriptorSetLayout* GetOrCreateDescriptorSetLayout(uint32_t setIndex);
		
	protected:
		vector<VkPushConstantRange> _pushConstantRanges;

		vector<VkVertexInputBindingDescription> _vertexBindings;
		vector<VkVertexInputAttributeDescription> _vertexAttributes;
		
	private:
		Device& _device;

		string _pass;

		uint32_t _hash;

		VkShaderModule _vertShaderModule;
		VkShaderModule _fragShaderModule;
		VkShaderModule _computeShaderModule;

		VkPipelineLayout _pipelineLayout;
		
		// Manage descriptor set layout objects by set index
		unordered_map<uint32_t, DescriptorSetLayout*> _descriptorSetLayouts;

		vector<string> _vertexAttributeNames;
	};
}