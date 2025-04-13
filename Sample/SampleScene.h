#pragma once
#include "Foundation/Scene.h"

namespace Core
{
	class GLTFLoader;
}

class SampleScene : public Core::Scene
{
public:
	SampleScene(Core::Device& device, float width, float height);
	virtual ~SampleScene() override;

	virtual void Update() override;
private:
	unique_ptr<Core::GLTFLoader> _gltfLoader;
};

