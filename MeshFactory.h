#pragma once

namespace Core
{
	class Mesh;
	class MeshFactory
	{
	public:
		static Mesh* CreateMesh(Device& device, string path);
		static void DeleteCache();
	private:
		static unordered_map<string, Mesh*> _meshCache;
	};
}

