#pragma once
#include "Component.h"

struct Vertex;

namespace Core
{
	class Buffer;
	class Shader;
	class CommandBuffer;

	struct ModelWorld
	{
		alignas(16) mat4 World;
	};

	class Mesh : public Component
	{
	public:
		Mesh(Entity& entity, VkCommandPool commandPool, string modelPath);
		~Mesh();

		virtual void UpdateFrame(float deltaTime) override;
		virtual std::type_index GetType() override;
		virtual void Resize(uint32_t width, uint32_t height) override;

		void DrawFrame(CommandBuffer& commandBuffer, Shader& shader);

		uint32_t GetIndexCount() const 
		{
			return static_cast<uint32_t>(_indices.size());
		}

		Buffer& GetVertexBuffer() { return *_vertexBuffer; }
		Buffer& GetIndexBuffer() { return *_indexBuffer; }
	private:
		void LoadModel(const string& modelPath);
		void CreateVertexBuffer(VkCommandPool commandPool);
		void CreateIndexBuffer(VkCommandPool commandPool);
	private:
		//clockwise.
		vector<Vertex> _vertices;
		vector<uint32_t> _indices;

		Buffer* _vertexBuffer;
		Buffer* _indexBuffer;

		ModelWorld _modelPushConstant;
	};
}
