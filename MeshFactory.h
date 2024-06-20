#pragma once

namespace Core
{
	class Mesh;
	class MeshFactory
	{
	public:
		static Mesh* CreateMesh(Device& device, string path);
		static Mesh* CreateMesh(Device& device, int polygonType);
		static void DeleteCache();
	private:
		static unordered_map<string, Mesh*> _meshCache;
		static unordered_map<int, Mesh*> _polygonCache;
	};
}

