#pragma once
#include "Component.h"

namespace Core
{
	class Mesh;
	class Material;
	class Pipeline;
	class MeshRenderer : public Component
	{
	public:
		MeshRenderer(Device& device);
		~MeshRenderer();

		void SetMaterial(Material* material);

		virtual void UpdateFrame(float deltaTime) override;
		virtual std::type_index GetType() override;
		virtual void Resize(uint32_t width, uint32_t height) override;
	private:
		Material* _material;
		Pipeline* _pipeline;
		Mesh* _mesh;
	};
}

