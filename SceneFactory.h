#pragma once
#include "Scene.h"

class SceneFactory
{
public:
	static Core::Scene* CreateScene(string name, 
		float width, float height, VkCommandPool commandPool);
};

