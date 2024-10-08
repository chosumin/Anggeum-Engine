#include "stdafx.h"
#include "Transform.h"
#include <glm/gtx/matrix_decompose.hpp>

Core::Transform::Transform(Entity& entity)
	:Component(entity)
{
}

std::type_index Core::Transform::GetType()
{
	return typeid(Transform);
}

void Core::Transform::SetTranslation(const glm::vec3& translation)
{
	_translation = translation;
	InvalidateWorldMatrix();
}

void Core::Transform::SetRotation(const glm::quat& rotation)
{
	_rotation = rotation;
	InvalidateWorldMatrix();
}

void Core::Transform::SetScale(const glm::vec3& scale)
{
	_scale = scale;
	InvalidateWorldMatrix();
}

const glm::vec3& Core::Transform::GetTranslation() const
{
	return _translation;
}

const glm::quat& Core::Transform::GetRotation() const
{
	return _rotation;
}

const glm::vec3& Core::Transform::GetScale() const
{
	return _scale;
}

void Core::Transform::SetMatrix(const glm::mat4& matrix)
{
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(matrix, _scale, _rotation, _translation, skew, perspective);

	InvalidateWorldMatrix();
}

glm::mat4 Core::Transform::GetMatrix() const
{
	return glm::translate(glm::mat4(1.0), _translation) *
		glm::mat4_cast(_rotation) *
		glm::scale(glm::mat4(1.0), _scale);
}

//glm::mat4 Core::Transform::GetWorldMatrix()
//{
//	UpdateWorldTransform();
//	return _worldMatrix;
//}

void Core::Transform::InvalidateWorldMatrix()
{
	_updateWorldMatrix = true;
}

void Core::Transform::Resize(uint32_t width, uint32_t height)
{
}

void Core::Transform::Rotate(const glm::vec3& deltaAngle)
{
	glm::quat qx = glm::angleAxis(deltaAngle.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::quat qy = glm::angleAxis(deltaAngle.y, glm::vec3(0.0f, -1.0f, 0.0f));

	_rotation = glm::normalize(qy * _rotation * qx);
}

void Core::Transform::Translate(const glm::vec4& delta)
{
	_translation.x += delta.x;
	_translation.y += delta.y;
	_translation.z += delta.z;
}

void Core::Transform::UpdateFrame(float deltaTime)
{
}

//void Core::Transform::UpdateWorldTransform()
//{
//	if (!_updateWorldMatrix)
//		return;
//
//	_worldMatrix = GetMatrix();
//
//	auto parent = node.get_parent();
//
//	if (parent)
//	{
//		auto& transform = parent->get_component<Transform>();
//		world_matrix = transform.get_world_matrix() * world_matrix;
//	}
//
//	update_world_matrix = false;
//}
