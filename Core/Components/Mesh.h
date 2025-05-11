#pragma once
#include "Foundation/Component.h"

namespace Core
{
	class SubMesh;
	class Material;
	class Mesh : public Component
	{
	public:
		Mesh(Device& device);
		~Mesh();

		const vector<shared_ptr<Material>>& GetMaterials() const { return _materials; }
		const vector<shared_ptr<SubMesh>>& GetSubMeshes() const { return _subMeshes; }

		string GetModelPath() const { return _modelPath; }

		void AddSubMesh(shared_ptr<SubMesh> subMesh);
		void AddMaterial(shared_ptr<Material> material);

		void UpdateFrame(float deltaTime) override;
		std::type_index GetType() override;
		void Resize(uint32_t width, uint32_t height) override;
	private:
		string _name;
		string _modelPath;

		Device& _device;

		vector<shared_ptr<Material>> _materials;
		vector<shared_ptr<SubMesh>> _subMeshes;
	};
}
