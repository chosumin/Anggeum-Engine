#pragma once
#include "Component.h"

namespace Core
{
	enum class MaterialType;
	class Mesh;
	class Material;
	class Pipeline;
	class CommandBuffer;
	class MeshRenderer : public Component
	{
	public:
		MeshRenderer(Device& device, Entity& entity);
		~MeshRenderer();

		Mesh& GetMesh() { return *_mesh; }
		Material& GetMaterial() { return* _material; }

		void SetMesh(int polygonType);
		void SetMesh(string path);
		void SetMaterial(string path);

		virtual void UpdateFrame(float deltaTime) override;
		virtual std::type_index GetType() override;
		virtual void Resize(uint32_t width, uint32_t height) override;
	private:
		Device& _device;
		Material* _material;
		Mesh* _mesh;
	};
}

