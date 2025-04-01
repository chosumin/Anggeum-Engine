#include "stdafx.h"
#include "PerspectiveCamera.h"
#include "Transform.h"
#include "Entity.h"

Core::PerspectiveCamera::PerspectiveCamera(float width, float height)
{
	_fov = radians(60.0f);
	_aspectRatio = width / height;
	_nearPlane = 0.1f;
	_farPlane = 1000.0f;

	SetPerspective();
}

Core::PerspectiveCamera::PerspectiveCamera()
{
}

type_index Core::PerspectiveCamera::GetType()
{
	return typeid(PerspectiveCamera);
}

void Core::PerspectiveCamera::SetAspectRatio(float aspectRatio)
{
	_aspectRatio = aspectRatio;
	SetPerspective();
}

void Core::PerspectiveCamera::SetFieldOfView(float fov)
{
	_fov = fov;
	SetPerspective();
}

float Core::PerspectiveCamera::GetFarPlane() const
{
	return _farPlane;
}

void Core::PerspectiveCamera::SetFarPlane(float zfar)
{
	_farPlane = zfar;
	SetPerspective();
}

float Core::PerspectiveCamera::GetNearPlane() const
{
	return _nearPlane;
}

void Core::PerspectiveCamera::SetNearPlane(float znear)
{
	_nearPlane = znear;
	SetPerspective();
}

float Core::PerspectiveCamera::GetAspectRatio()
{
	return _aspectRatio;
}

float Core::PerspectiveCamera::GetFieldOfView()
{
	return _fov;
}

mat4 Core::PerspectiveCamera::GetProjection()
{
	return Matrices.Perspective;
}

mat4 Core::PerspectiveCamera::GetView()
{
	return Matrices.View;
}

const mat4 Core::PerspectiveCamera::GetPreRotation()
{
	return _preRotation;
}

void Core::PerspectiveCamera::SetPreRotation(const glm::mat4& pre_rotation)
{
	_preRotation = pre_rotation;
}

void Core::PerspectiveCamera::SetPerspective()
{
	Matrices.Perspective = perspective(
		_fov,
		_aspectRatio,
		_nearPlane, _farPlane);

	//Flip Y in clipspace.
	Matrices.Perspective[1][1] *= -1;
}

void Core::PerspectiveCamera::UpdateFrame(float deltaTime)
{
	auto& transform = _entity->GetComponent<Transform>();
	Matrices.View = transform.GetMatrix();
	Matrices.Position = vec3(glm::inverse(Matrices.View)[3]);
}

void Core::PerspectiveCamera::Resize(uint32_t width, uint32_t height)
{
}
