#pragma once
#include "Scene.h"

class SampleScene : public Core::Scene
{
public:
	SampleScene(Core::Device& device, float width, float height);
	virtual ~SampleScene() override;
};

