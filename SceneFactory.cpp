#include "stdafx.h"
#include "SceneFactory.h"
#include "SampleScene.h"
#include "CommandPool.h"

constexpr unsigned int HashCode(const char* str)
{
	return str[0] ? static_cast<unsigned int>(str[0]) + 0xEDB8832Full * HashCode(str + 1) : 8603;
}

Core::Scene* SceneFactory::CreateScene(string name, float width, float height, Core::CommandPool& commandPool)
{
	int hashCode = HashCode(name.c_str());

	switch (hashCode)
	{
	case HashCode("SampleScene"):
		return new SampleScene(width, height, commandPool);
	default:
		break;
	}

	return nullptr;
}
