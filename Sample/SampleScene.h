#pragma once
#include "Foundation/Scene.h"

namespace Core
{
	class GLTFLoader;
	class TransferContext;
}

class SampleScene : public Core::Scene
{
public:
	SampleScene(Core::Device& device, float width, float height, Core::TransferContext* transferContext);
	virtual ~SampleScene() override;

	virtual void Update() override;
private:
	unique_ptr<Core::GLTFLoader> _gltfLoader;
};

