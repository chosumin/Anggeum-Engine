#pragma once

struct Vertex 
{
    vec3 Pos;
    vec3 Color;
    vec2 TexCoord;

    static VkVertexInputBindingDescription GetBindingDescription() 
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static vector<VkVertexInputAttributeDescription> GetAttributeDescriptions() {
        vector<VkVertexInputAttributeDescription> attributeDescriptions(3);

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, Pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, Color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, TexCoord);

        return attributeDescriptions;
    }

    bool operator==(const Vertex& other) const
    {
        return Pos == other.Pos && Color == other.Color && TexCoord == other.TexCoord;
    }
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