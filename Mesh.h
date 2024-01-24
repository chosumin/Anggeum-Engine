#pragma once
#include "Component.h"

struct Vertex;

namespace Core
{
	class Buffer;
	class ModelUniformBuffer;
	class Texture;
	class IDescriptor;
	class Mesh : public Component
	{
	public:
		Mesh(Entity& entity, VkCommandPool commandPool, string modelPath);
		~Mesh();

		virtual void UpdateFrame(float deltaTime) override;
		virtual std::type_index GetType() override;
		virtual void Resize(uint32_t width, uint32_t height) override;

		void DrawFrame(VkCommandBuffer commandBuffer, uint32_t index) const;

		vector<IDescriptor*> GetDescriptors() const;
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

		ModelUniformBuffer* _buffer;
		Core::Texture* _texture;

		int _type = 0;
	};
}
