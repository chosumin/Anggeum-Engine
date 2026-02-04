#include "stdafx.h"
#include "SpirvUtility.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Vertex.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Utils/Utility.h"
#include "shaderc/shaderc.hpp"
#include "spirv_cross/spirv_cross.hpp"

class FileIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
    shaderc_include_result* GetInclude(const char* requested_source,
        shaderc_include_type type,
        const char* requesting_source,
        size_t include_depth) override 
    {
        std::string full_path = std::string("Assets/shaders/") + requested_source;

        std::ifstream file(full_path, ios::binary);
        if (!file.is_open()) 
        {
            return MakeErrorIncludeResult("Cannot open include file: " + full_path);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        return MakeIncludeResult(full_path, content);
    }

    void ReleaseInclude(shaderc_include_result* include_result) override 
    {
        delete[] include_result->source_name;
        delete[] include_result->content;
        delete include_result;
    }

private:
    shaderc_include_result* MakeIncludeResult(const std::string& name, const std::string& content) 
    {
        auto* result = new shaderc_include_result();
        result->source_name = CopyString(name);
        result->source_name_length = name.size();
        result->content = CopyString(content);
        result->content_length = content.size();
        result->user_data = nullptr;
        return result;
    }

    shaderc_include_result* MakeErrorIncludeResult(const std::string& message) 
    {
        auto* result = new shaderc_include_result();
        result->source_name = CopyString("ERROR");
        result->source_name_length = 5;
        result->content = CopyString(message);
        result->content_length = message.size();
        result->user_data = nullptr;
        return result;
    }

    char* CopyString(const std::string& str) 
    {
        char* data = new char[str.size() + 1];
        memcpy(data, str.c_str(), str.size() + 1);
        return data;
    }
};

vector<unsigned int> Core::SpirvUtility::GLSLToSPV(VkShaderStageFlagBits shaderStage, const char* shaderCode, const string& shaderPath, bool gpuDrivenEnabled)
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

    options.SetIncluder(std::make_unique<FileIncluder>());
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

    options.SetOptimizationLevel(shaderc_optimization_level_zero);
    options.SetGenerateDebugInfo();

    shaderc_shader_kind kind = shaderc_vertex_shader;

    switch (shaderStage)
    {
    case VK_SHADER_STAGE_VERTEX_BIT:
        kind = shaderc_vertex_shader;
        break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
        kind = shaderc_tess_control_shader;
        break;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
        kind = shaderc_tess_evaluation_shader;
        break;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        kind = shaderc_geometry_shader;
        break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        kind = shaderc_fragment_shader;
        break;
    case VK_SHADER_STAGE_COMPUTE_BIT:
        kind = shaderc_compute_shader;
        break;
    }

	// FIXME: Needs shader permutation system.
    string modifiedSource;
    /*if (gpuDrivenEnabled)
    {
        modifiedSource = "#define GPU_DRIVEN_RENDERING 1\n";
    }*/
    modifiedSource += shaderCode;

    // Compile GLSL to SPIR-V binary
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(modifiedSource, kind, shaderPath.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader compilation error: " << result.GetErrorMessage() << std::endl;
        return {};
    }

    return { result.cbegin(), result.cend() };
}

size_t GetTypeSizeBytes(spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type)
{
    // Get size of scalar type
    size_t scalar_size = 0;
    switch (type.basetype) {
    case spirv_cross::SPIRType::Float: scalar_size = 4; break;
    case spirv_cross::SPIRType::Int:   scalar_size = 4; break;
    case spirv_cross::SPIRType::UInt:  scalar_size = 4; break;
    case spirv_cross::SPIRType::Double:scalar_size = 8; break;
    default: scalar_size = 4; break; // Assume 4-byte for unknown
    }

    // Matrix types are arrays of vectors (column-major by default)
    if (type.columns > 1) {
        // Each column is a vector of size `vecsize`
        // Example: mat4 = 4 columns of vec4 �� 4x4x4 bytes
        return scalar_size * type.vecsize * type.columns;
    }

    // For vectors (vec2, vec3, vec4)
    return scalar_size * type.vecsize;
}

void Core::SpirvUtility::SetResources(Shader& shader, VkShaderStageFlagBits shaderStage, 
    const string& shaderPath, const vector<uint32_t>& spirvBinary)
{
    cout << "Reflecting shader: "<< shaderPath << endl;

    spirv_cross::Compiler compiler(spirvBinary);
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    for (auto& uniformBuffer : resources.uniform_buffers) 
    {
        unsigned set = compiler.get_decoration(uniformBuffer.id, spv::DecorationDescriptorSet);
        unsigned binding = compiler.get_decoration(uniformBuffer.id, spv::DecorationBinding);
        const spirv_cross::SPIRType& type = compiler.get_type(uniformBuffer.base_type_id);
        size_t size = compiler.get_declared_struct_size(type);

        shader.AddUniformBufferLayoutBinding(set, binding, shaderStage, size);
    }

    spirv_cross::SmallVector<spirv_cross::SpecializationConstant> specConstants = compiler.get_specialization_constants();
	for (size_t i = 0; i < resources.storage_buffers.size(); ++i)
	{
        auto& storageBuffer = resources.storage_buffers[i];
        unsigned set = compiler.get_decoration(storageBuffer.id, spv::DecorationDescriptorSet);
        unsigned binding = compiler.get_decoration(storageBuffer.id, spv::DecorationBinding);

        shader.AddStorageBufferLayoutBinding(set, binding, shaderStage);
    }

    for (const auto& resource : resources.sampled_images) 
    {
        uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

        // Check if this is the bindless texture array (Set 2, Binding 0)
        if (set == static_cast<uint32_t>(DescriptorSetType::Bindless))
        {
            // Check if it's an array
            const auto& type = compiler.get_type(resource.type_id);
            if (type.array.size() > 0)
            {
                cout << "Shader '" << shaderPath << "' uses bindless textures (Set "
                    << set << ", Array size: " << type.array[0] << ")" << endl;
                shader.EnableBindlessTextures();
            }
        }

        shader.AddTextureBufferLayoutBinding(set, binding, shaderStage);
    }

    for (const auto& resource : resources.push_constant_buffers) 
    {
        string name = compiler.get_name(resource.id);
        const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
        uint32_t size = Utility::ToU32(compiler.get_declared_struct_size(type));

        shader.AddPushConstantsRange(shaderStage, size);
    }

	if (shaderStage == VK_SHADER_STAGE_VERTEX_BIT)
	{
        map<uint32_t, pair<string, uint32_t>> vertexInputs;

        size_t size = resources.stage_inputs.size();

        for (size_t i = 0; i < size; ++i)
        {
            auto input = resources.stage_inputs[i];
            string name = compiler.get_name(input.id);

            Utility::ConvertLowerCase(name, name);

            uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
            const spirv_cross::SPIRType& type = compiler.get_type(input.type_id);
            uint32_t inputSize = Utility::ToU32(GetTypeSizeBytes(compiler, type));

            vertexInputs[location] = { name, inputSize };
		}

        for (auto&& input : vertexInputs)
        {
            string name = input.second.first;
            if (name.find("pos") != string::npos)
            {
                shader._vertexAttributeNames.push_back(VertexAttributeName::Position);
            }
            else if (name.find("normal") != string::npos)
            {
                shader._vertexAttributeNames.push_back(VertexAttributeName::Normal);
            }
            else if (name.find("col") != string::npos)
            {
                shader._vertexAttributeNames.push_back(VertexAttributeName::Col);
            }
            else if (name.find("texcoord") != string::npos || 
                name.find("uv") != string::npos)
            {
                shader._vertexAttributeNames.push_back(VertexAttributeName::UV);
            }

            shader._vertexBindings.push_back(Vertex::GetBindingDescription(input.first, input.second.second, VK_VERTEX_INPUT_RATE_VERTEX));

            VkFormat format;
            switch (input.second.second)
            {
            case 4: 
                format = VK_FORMAT_R32_SFLOAT;
                break;
            case 8:
                format = VK_FORMAT_R32G32_SFLOAT;
                break;
            case 12:
                format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case 16:
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            default:
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            }

            shader._vertexAttributes.push_back(Vertex::GetAttributeDescription(input.first, input.first, format, 0));
        }
	}
}
