#pragma once

struct Vertex;

namespace Core
{
	class Buffer;
	struct Transform;
	class Polygon
	{
	public:
		Polygon(VkCommandPool commandPool, vec3 position, string modelPath);
		~Polygon();

		void DrawFrame(VkCommandBuffer commandBuffer) const;
	private:
		void LoadModel(const string& modelPath);
		void CreateVertexBuffer(VkCommandPool commandPool);
		void CreateIndexBuffer(VkCommandPool commandPool);
	private:
		//clockwise.
		vector<Vertex> _vertices;
		vector<uint32_t> _indices;

		Core::Buffer* _vertexBuffer;
		Core::Buffer* _indexBuffer;

		unique_ptr<Transform> _transform;
	};
}
