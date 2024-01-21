#include "stdafx.h"
#include "PerspectiveCamera.h"
#include "Transform.h"

Core::PerspectiveCamera::PerspectiveCamera(const std::string& name,
	float width, float height, weak_ptr<Transform> transform)
	:UniformBuffer(sizeof(VPBufferObject), 0)
{
	_matrices.View = lookAt(
		vec3(2.0f, 2.0f, 2.0f),
		vec3(0.0f, 0.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f));

	_matrices.Perspective = perspective(
		radians(45.0f),
		width / height,
		0.1f, 10.0f);
	_matrices.Perspective[1][1] *= -1;

	_transform = transform;
	_transform.lock()->SetMatrix(_matrices.View);
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
	return _matrices.Perspective;
}

mat4 Core::PerspectiveCamera::GetView()
{
	return _matrices.View;
}

const mat4 Core::PerspectiveCamera::GetPreRotation()
{
	return _preRotation;
}

void Core::PerspectiveCamera::SetPreRotation(const glm::mat4& pre_rotation)
{
	_preRotation = pre_rotation;
}

void Core::PerspectiveCamera::MapBufferMemory(void* uniformBufferMapped)
{
	auto transform = *_transform.lock();
	_matrices.View = transform.GetMatrix();
	memcpy(uniformBufferMapped, &_matrices, sizeof(_matrices));
}
