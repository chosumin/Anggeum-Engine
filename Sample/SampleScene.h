#pragma once
#include "Foundation/Scene.h"

namespace Core
{
	class GLTFLoader;
	class TransferContext;
	class RenderContext;
}

class SampleScene : public Core::Scene
{
public:
	SampleScene(Core::Device& device, float width, float height, Core::TransferContext* transferContext, Core::RenderContext* renderContext);
	~SampleScene();

	virtual void Update() override;
private:
	unique_ptr<Core::GLTFLoader> _gltfLoader;
	Core::RenderContext* _renderContext;
	Core::Device& _device;
};

