#pragma once
#include "Foundation/Scene.h"

namespace Core
{
	class GLTFLoader;
	class TransferThread;
}

class SampleScene : public Core::Scene
{
public:
	SampleScene(Core::Device& device, float width, float height, Core::TransferThread* transferThread);
	virtual ~SampleScene() override;

	virtual void Update() override;
private:
	unique_ptr<Core::GLTFLoader> _gltfLoader;
};

