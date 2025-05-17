#pragma once
#include "UniformBuffer.h"
#include "TextureBuffer.h"
#include "Graphics/Vulkans/Vertex.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class IDescriptor;
	class DescriptorPool;
	class Shader
	{
	private:
		friend class SpirvUtility;
	public:
		Shader(Device& device, 
			const string pass,
			const string& vertFilePath,
			const string& fragFilePath);
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

		VkShaderStageFlags GetPushConstantsShaderStage(uint32_t index) const;
		uint32_t GetPushConstantsOffset(uint32_t index) const;
		vector<VkPushConstantRange>& GetPushConstantRanges() { return _pushConstantRanges; }
		VkDescriptorSetLayout& GetDescriptorSetLayout();
		VkDescriptorPool& GetDescriptorPool();
		VkPipelineLayout GetPipelineLayout() { return _pipelineLayout; }

		const vector<UniformBufferLayoutBinding>& GetUniformBufferLayoutBindings() const
		{
			return _uniformBufferLayoutBindings;
		}

		const vector<TextureBufferLayoutBinding>& GetTextureBufferLayoutBindings() const
		{
			return _textureBufferLayoutBindings;
		}
	protected:
		void AddUniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage, VkDeviceSize size);
		void AddTextureBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage);
		void AddPushConstantsRange(VkShaderStageFlags stage, uint32_t size);
	private:
		void CreateDescriptorPool();
		void SetResources(const string vertPath, const vector<uint32_t>& vertSpirvBinary,
			const string fragPath, const vector<uint32_t>& fragSpirvBinary);

		VkShaderModule CreateShaderModule(VkDevice& device, const vector<uint32_t>& code, size_t codeSize) const;
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

		DescriptorPool* _descriptorPool;

		VkPipelineLayout _pipelineLayout;

		vector<UniformBufferLayoutBinding> _uniformBufferLayoutBindings;
		vector<TextureBufferLayoutBinding> _textureBufferLayoutBindings;

		vector<string> _vertexAttributeNames;
	};
}