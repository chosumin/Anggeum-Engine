#pragma once
#include "Scene.h"

namespace Core
{
	class CommandPool;
}
class SampleScene : public Core::Scene
{
public:
	SampleScene(float width, float height, Core::CommandPool& commandPool);
};

