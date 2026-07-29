#pragma once
#include "Foundation/Scene.h"

namespace Core
{
	class GLTFLoader;
	class RenderContext;
}

class SampleScene : public Core::Scene
{
public:
	SampleScene(Core::Device& device, float width, float height, Core::RenderContext* renderContext);
	~SampleScene();

	virtual void Update() override;
private:
	unique_ptr<Core::GLTFLoader> _gltfLoader;
	Core::RenderContext* _renderContext;
	Core::Device& _device;

private:
	glm::vec3 _dirLightEuler{ 45.0f, 45.0f, 0.0f };

	std::vector<glm::vec3> _pointLightCenters;
	std::vector<glm::vec3> _spotLightCenters;
	std::vector<float> _pointLightAngles;
	std::vector<float> _spotLightAngles;
	std::vector<float> _pointLightSpeeds;
	std::vector<float> _spotLightSpeeds;
	std::vector<float> _pointLightRadii;
	std::vector<float> _spotLightRadii;
};

