#pragma once
#include "Scene.h"

class SceneFactory
{
public:
	static Core::Scene* CreateScene(Core::Device& device, 
		string name, float width, float height);
};

