#include "stdafx.h"
#include "Camera.h"
#include "InputEvents.h"
#include "PerspectiveCamera.h"
#include "FreeCamera.h"
#include "Transform.h"

Core::Camera::Camera(float width, float height)
{
	_transform = make_shared<Transform>();
	_perspectiveCamera = make_unique<PerspectiveCamera>("camera", width, height, _transform);
	_freeCamera = make_unique<FreeCamera>(_transform);
}

Core::Camera::~Camera()
{
}

void Core::Camera::UpdateFrame(float deltaTime)
{
	_freeCamera->UpdateFrame(deltaTime);
}

void Core::Camera::InputEvent(const Core::InputEvent& inputEvent)
{
	_freeCamera->InputEvent(inputEvent);
}

Core::UniformBuffer* Core::Camera::GetUniformBuffer() const
{
	return _perspectiveCamera.get();
}
