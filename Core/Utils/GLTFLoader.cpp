#include "stdafx.h"
#include "GLTFLoader.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParties/tinygltf/tiny_gltf.h"

void Core::GLTFLoader::Load()
{
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	string err;
	string warn;

	string path = "./Assets/Models/GlassHurricaneCandleHolder/glTF/GlassHurricaneCandleHolder.gltf";
	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);

	int a = 10;
}
