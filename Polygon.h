#pragma once

struct Vertex;

namespace Core
{
	class Buffer;
	class Transform;
	class Polygon
	{
	public:
		Polygon(VkCommandPool commandPool, vec3 position);
		~Polygon();

		void BindBuffers(VkCommandBuffer commandBuffer) const;
		void DrawFrame(VkCommandBuffer commandBuffer) const;
	private:
		void CreateVertexBuffer(VkCommandPool commandPool);
		void CreateIndexBuffer(VkCommandPool commandPool);
	private:
		//clockwise.
		vector<Vertex> _vertices;
		vector<uint16_t> _indices = 
		{ 
			0, 1, 2, 2, 3, 0, 
			4, 5, 6, 6, 7, 4
		};

		Core::Buffer* _vertexBuffer;
		Core::Buffer* _indexBuffer;

		unique_ptr<Transform> _transform;
	};
}
