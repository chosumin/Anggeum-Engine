#include "stdafx.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "SpirvCompiler.h"
#include "shaderc/shaderc.hpp"

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

vector<unsigned int> Core::SpirvCompiler::GLSLToSPV(VkShaderStageFlagBits shaderStage, const char* shaderCode, const string& shaderPath)
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

    options.SetIncluder(std::make_unique<FileIncluder>());
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

    // Enable optimization (optional)
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

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

    // Compile GLSL to SPIR-V binary
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(shaderCode, kind, shaderPath.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader compilation error: " << result.GetErrorMessage() << std::endl;
        return {};
    }

    return { result.cbegin(), result.cend() };
}
