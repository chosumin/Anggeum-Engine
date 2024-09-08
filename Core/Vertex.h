#pragma once

struct VertexAttributeName
{
    static string Position;
    static string Col;
    static string UV;
};

struct Vertex 
{
    vec3 Pos;
    vec3 Color;
    vec2 TexCoord;

    static VkVertexInputBindingDescription GetBindingDescription(
        uint32_t binding, uint32_t stride, VkVertexInputRate inputRate) 
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = binding;
        bindingDescription.stride = stride;
        bindingDescription.inputRate = inputRate;
        return bindingDescription;
    }

    static VkVertexInputAttributeDescription GetAttributeDescription(
        uint32_t binding, uint32_t location, VkFormat format, uint32_t offset)
    {
        VkVertexInputAttributeDescription attributeDescription{};

        attributeDescription.binding = binding;
        attributeDescription.location = location;
        attributeDescription.format = format;
        attributeDescription.offset = offset;

        return attributeDescription;
    }

    bool operator==(const Vertex& other) const
    {
        return Pos == other.Pos && Color == other.Color && TexCoord == other.TexCoord;
    }
};

struct ShadowVertex
{
    vec3 Pos;
};

namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            return 
                ((hash<glm::vec3>()(vertex.Pos) ^ 
                (hash<glm::vec3>()(vertex.Color) << 1)) >> 1) ^ 
                (hash<glm::vec2>()(vertex.TexCoord) << 1);
        }
    };
}