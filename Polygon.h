#pragma once

struct Vertex;

namespace Core
{
	class Buffer;
	class Transform;
	class ModelUniformBuffer;
	class Polygon
	{
	public:
		Polygon(VkCommandPool commandPool, vec3 position, string modelPath, ModelUniformBuffer* buffer);
		~Polygon();

		void DrawFrame(VkCommandBuffer commandBuffer, uint32_t index) const;
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
		ModelUniformBuffer* _buffer;
	};
}
