#pragma once
#include "Scene.h"

namespace Core
{
	class CommandPool;
}

class SceneFactory
{
public:
	static Core::Scene* CreateScene(string name, 
		float width, float height, Core::CommandPool& commandPool);
};

