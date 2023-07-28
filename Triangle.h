#pragma once

namespace Core
{
	class Buffer;
}

class Triangle
{
public:
	Triangle(VkCommandPool commandPool);
	~Triangle();

	void DrawFrame(VkCommandBuffer commandBuffer) const;
private:
	void CreateVertexBuffer(VkCommandPool commandPool);
	void CreateIndexBuffer(VkCommandPool commandPool);
private:
	//clockwise.
	vector<struct Vertex> _vertices;
	vector<uint16_t> _indices = { 0, 1, 2, 2, 3, 0 };

	Core::Buffer* _vertexBuffer;
	Core::Buffer* _indexBuffer;
};

