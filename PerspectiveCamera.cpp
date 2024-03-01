#include "stdafx.h"
#include "PerspectiveCamera.h"
#include "Transform.h"
#include "Entity.h"

Core::PerspectiveCamera::PerspectiveCamera(
	Entity& entity, float width, float height)
	//:UniformBuffer(sizeof(VPBufferObject), 0), Component(entity)
	:Component(entity)
{
	Matrices.View = lookAt(
		vec3(2.0f, 2.0f, 2.0f),
		vec3(0.0f, 0.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f));

	Matrices.Perspective = perspective(
		radians(60.0f),
		width / height,
		0.1f, 10.0f);
	Matrices.Perspective[1][1] *= -1;

	auto& transform = entity.GetComponent<Transform>();
	transform.SetMatrix(Matrices.View);
}

type_index Core::PerspectiveCamera::GetType()
{
	return typeid(PerspectiveCamera);
}

void Core::PerspectiveCamera::SetAspectRatio(float aspectRatio)
{
	_aspectRatio = aspectRatio;
}

void Core::PerspectiveCamera::SetFieldOfView(float fov)
{
	_fov = fov;
}

float Core::PerspectiveCamera::GetFarPlane() const
{
	return _farPlane;
}

void Core::PerspectiveCamera::SetFarPlane(float zfar)
{
	_farPlane = zfar;
}

float Core::PerspectiveCamera::GetNearPlane() const
{
	return _nearPlane;
}

void Core::PerspectiveCamera::SetNearPlane(float znear)
{
	_nearPlane = znear;
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

void Core::PerspectiveCamera::UpdateFrame(float deltaTime)
{
}

void Core::PerspectiveCamera::Resize(uint32_t width, uint32_t height)
{
}
