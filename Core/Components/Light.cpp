#include "stdafx.h"
#include "Light.h"

Core::Light::Light(const std::string& name)
	:_lightType(), _properties(), _name(name)
{
}

std::type_index Core::Light::GetType()
{
	return typeid(Light);
}

const Core::LightType& Core::Light::GetLightType()
{
	return _lightType;
}

void Core::Light::SetLightType(const LightType& type)
{
	_lightType = type;
}

Core::LightProperties& Core::Light::GetProperties()
{
	return _properties;
}

void Core::Light::SetProperties(const LightProperties& properties)
{
	_properties = properties;
}

void Core::Light::UpdateFrame(float deltaTime)
{
}

void Core::Light::Resize(uint32_t width, uint32_t height)
{
}
