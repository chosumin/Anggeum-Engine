#pragma once

namespace Core
{
	class UniformBuffer;
	class IPushConstant;
	class Shader
	{
	public:
		Shader(Device& device, 
			const string& vertFilePath,
			const string& fragFilePath);
		virtual ~Shader();

		virtual std::type_index GetType() = 0;

		vector<VkPipelineShaderStageCreateInfo> GetShaderStageCreateInfo() const;
		VkPipelineVertexInputStateCreateInfo GetVertexInputStateCreateInfo() const;
		VkPipelineLayout GetPipelineLayout() const
		{
			return _pipelineLayout;
		}
		VkDescriptorSet* GetDescriptorSet(size_t index) { return &_descriptorSets[index]; }
	protected:
		void CreateDescriptors(vector<IDescriptor*> descriptors);
		void CreateDescriptorSetLayout(vector<IDescriptor*> descriptors);
		void CreateDescriptorPool(vector<IDescriptor*> descriptors);
		void CreateDescriptorSets(vector<IDescriptor*> descriptors);
		void CreatePushConstants(vector<IPushConstant*> pushConstants);
		void CreatePipelineLayout();

		/*VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
		VkDescriptorType GetDescriptorType();*/
	private:
		vector<char> ReadFile(const string& filePath);
		VkShaderModule CreateShaderModule(VkDevice& device, const vector<char>& code) const;
	protected:
		unordered_map<uint32_t, UniformBuffer*> _uniformBuffers;
		//unordered_map<uint32_t, TextureBuffer*> _textureBuffers;
		//vector<IPushConstant*> _pushConstants;
	private:
		Device& _device;

		VkShaderModule _vertShaderModule;
		VkShaderModule _fragShaderModule;

		vector<VkDescriptorSet> _descriptorSets;
		VkDescriptorPool _descriptorPool;
		VkDescriptorSetLayout _descriptorSetLayout;
		vector<VkPushConstantRange> _pushConstants;
		VkPipelineLayout _pipelineLayout;
	};
}