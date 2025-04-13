#pragma once
#include "Foundation/Component.h"

namespace Core
{
	class SubMesh;
	class Material;
	class Mesh : public Component
	{
	public:
		Mesh(Device& device, string modelPath);
		Mesh(Device& device, int polygonType);
		Mesh(Device& device);
		~Mesh();

		const vector<Material*>& GetMaterials() const { return _materials; }
		const vector<SubMesh*>& GetSubMeshes() const { return _subMeshes; }

		string GetModelPath() const { return _modelPath; }

		void AddSubMesh(int polygonType);
		void AddSubMesh(string path);
		void AddSubMesh(SubMesh* subMesh);
		void AddMaterial(Material* material);
		void AddMaterial(string path);

		// Component을(를) 통해 상속됨
		void UpdateFrame(float deltaTime) override;
		std::type_index GetType() override;
		void Resize(uint32_t width, uint32_t height) override;
	private:
		void LoadModel(const string& modelPath);
		void LoadPlane();
	private:
		string _name;
		string _modelPath;

		Device& _device;

		vector<Material*> _materials;
		vector<SubMesh*> _subMeshes;
	};
}
