#include "stdafx.h"
#include "SceneFactory.h"
#include "SampleScene.h"

constexpr unsigned int HashCode(const char* str)
{
	return str[0] ? static_cast<unsigned int>(str[0]) + 0xEDB8832Full * HashCode(str + 1) : 8603;
}

Core::Scene* SceneFactory::CreateScene(Core::Device& device, 
	string name, float width, float height)
{
	int hashCode = HashCode(name.c_str());

	switch (hashCode)
	{
	case HashCode("SampleScene"):
		return new SampleScene(device, width, height);
	default:
		break;
	}

	return nullptr;
}
