#include "stdafx.h"
#include "FreeCamera.h"
#include "Transform.h"
#include "InputEvents.h"
#include "Entity.h"
using namespace Core;

const float TOUCH_DOWN_MOVE_FORWARD_WAIT_TIME = 2.0f;
const float ROTATION_MOVE_WEIGHT = 1.0f;
const float KEY_ROTATION_MOVE_WEIGHT = 0.5f;
const float TRANSLATION_MOVE_WEIGHT = 3.0f;
const float TRANSLATION_MOVE_STEP = 5.0f;
const uint32_t TRANSLATION_MOVE_SPEED = 3;

Core::FreeCamera::FreeCamera(Entity& entity)
	:Component(entity)
{
}

void Core::FreeCamera::UpdateFrame(float deltaTime)
{
	glm::vec3 deltaTranslation(0.0f, 0.0f, 0.0f);
	glm::vec3 deltaRotation(0.0f, 0.0f, 0.0f);

	float mulTranslation = 1.0f;

	auto& keyPressed = Core::Input::KeyPressed;

	//todo : translate
	Translate(deltaTranslation, mulTranslation);

	if (keyPressed[KeyCode::I])
	{
		deltaRotation.x += KEY_ROTATION_MOVE_WEIGHT;
	}
	if (keyPressed[KeyCode::K])
	{
		deltaRotation.x -= KEY_ROTATION_MOVE_WEIGHT;
	}
	if (keyPressed[KeyCode::J])
	{
		deltaRotation.y += KEY_ROTATION_MOVE_WEIGHT;
	}
	if (keyPressed[KeyCode::L])
	{
		deltaRotation.y -= KEY_ROTATION_MOVE_WEIGHT;
	}

	auto& mouseButtonPressed = Core::Input::MouseButtonPressed;
	auto& mouseMoveDelta = Core::Input::MouseMoveDelta;
	if (mouseButtonPressed[MouseButton::Left] && mouseButtonPressed[MouseButton::Right])
	{
		deltaRotation.z += TRANSLATION_MOVE_WEIGHT * mouseMoveDelta.x;
	}
	else if (mouseButtonPressed[MouseButton::Right])
	{
		deltaRotation.x -= ROTATION_MOVE_WEIGHT * mouseMoveDelta.y;
		deltaRotation.y -= ROTATION_MOVE_WEIGHT * mouseMoveDelta.x;
	}
	/*else if (mouseButtonPressed[MouseButton::Left])
	{
		deltaTranslation.x += TRANSLATION_MOVE_WEIGHT * mouseMoveDelta.x;
		deltaTranslation.y += TRANSLATION_MOVE_WEIGHT * -mouseMoveDelta.y;
	}*/

	deltaTranslation *= mulTranslation * deltaTime;
	deltaRotation *= deltaTime;

	// Only re-calculate the transform if it's changed
	if (deltaRotation != glm::vec3(0.0f, 0.0f, 0.0f) || deltaTranslation != glm::vec3(0.0f, 0.0f, 0.0f))
	{
		auto& transform = _entity.GetComponent<Transform>();
		
		glm::quat qx = glm::angleAxis(deltaRotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::quat qy = glm::angleAxis(deltaRotation.y, glm::vec3(0.0f, -1.0f, 0.0f));

		glm::quat orientation = glm::normalize(qy * transform.GetRotation() * qx);

		transform.SetTranslation(transform.GetTranslation() + deltaTranslation * glm::conjugate(orientation));
		transform.SetRotation(orientation);
	}
}

void Core::FreeCamera::Resize(uint32_t width, uint32_t height)
{
}

type_index Core::FreeCamera::GetType()
{
	return typeid(FreeCamera);
}


void Core::FreeCamera::Translate(vec3& delta, float& multiplier)
{
	auto& keyPressed = Core::Input::KeyPressed;

	if (keyPressed[KeyCode::W])
	{
		delta.z -= TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::S])
	{
		delta.z += TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::A])
	{
		delta.x -= TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::D])
	{
		delta.x += TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::Q])
	{
		delta.y -= TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::E])
	{
		delta.y += TRANSLATION_MOVE_STEP;
	}

	multiplier = 1.0f;

	if (keyPressed[KeyCode::LeftControl])
	{
		multiplier *= (1.0f * TRANSLATION_MOVE_SPEED);
	}
	if (keyPressed[KeyCode::LeftShift])
	{
		multiplier *= (1.0f / TRANSLATION_MOVE_SPEED);
	}
}
