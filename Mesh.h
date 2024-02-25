#pragma once
#include "Component.h"
#include "IPushConstant.h"

struct Vertex;

namespace Core
{
	class Buffer;
	class ModelPushConstants;
	class Texture;
	class IDescriptor;
	class ModelPushConstant;
	class Shader;
	class CommandBuffer;

	struct ModelWorld
	{
		alignas(16) mat4 World;
	};

	class ModelPushConstant : public IPushConstant
	{
	public:
		ModelPushConstant();

		virtual uint32_t GetSize() const;
		virtual VkPushConstantRange GetPushConstantRange() const override;
		virtual uint32_t GetOffset() const;
		virtual void SetOffset(uint32_t offset);
	public:
		ModelWorld Buffer;
	private:
		uint32_t _offset;
	};

	class Mesh : public Component
	{
	public:
		Mesh(Entity& entity, VkCommandPool commandPool, string modelPath);
		~Mesh();

		virtual void UpdateFrame(float deltaTime) override;
		virtual std::type_index GetType() override;
		virtual void Resize(uint32_t width, uint32_t height) override;

		void DrawFrame(CommandBuffer& commandBuffer, Shader& shader) const;

		IDescriptor* GetDescriptor() const;
		ModelPushConstant* GetPushConstant() const;
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

		ModelPushConstant* _modelPushConstant;
		Texture* _texture;
	};
}
