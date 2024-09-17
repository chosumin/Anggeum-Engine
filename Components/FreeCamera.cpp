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
	glm::vec4 deltaTranslation(0.0f, 0.0f, 0.0f, 1.0f);
	glm::vec3 deltaRotation(0.0f, 0.0f, 0.0f);

	float mulTranslation = 1.0f;

	Translate(deltaTranslation, mulTranslation);
	Rotate(deltaRotation);

	deltaTranslation *= mulTranslation * deltaTime;
	deltaTranslation.w = 1.0f;

	deltaRotation *= deltaTime;

	// Only re-calculate the transform if it's changed
	if (deltaRotation != glm::vec3(0.0f, 0.0f, 0.0f) || deltaTranslation != glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
	{
		auto& transform = _entity.GetComponent<Transform>();

		transform.Rotate(deltaRotation);

		auto rotMat = toMat4(transform.GetRotation());
		auto translation = rotMat * deltaTranslation;
		transform.Translate(deltaTranslation);
	}
}

void Core::FreeCamera::Resize(uint32_t width, uint32_t height)
{
}

type_index Core::FreeCamera::GetType()
{
	return typeid(FreeCamera);
}


void Core::FreeCamera::Translate(vec4& delta, float& multiplier)
{
	auto& keyPressed = Core::Input::KeyPressed;

	if (keyPressed[KeyCode::W])
	{
		delta.z += TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::S])
	{
		delta.z -= TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::A])
	{
		delta.x += TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::D])
	{
		delta.x -= TRANSLATION_MOVE_STEP;
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

void Core::FreeCamera::Rotate(vec3& delta)
{
	auto& mouseButtonPressed = Core::Input::MouseButtonPressed;

	if (mouseButtonPressed[MouseButton::Right] == false)
		return;

	auto& mouseMoveDelta = Core::Input::MouseMoveDelta;

	delta.x -= ROTATION_MOVE_WEIGHT * mouseMoveDelta.y;
	delta.y -= ROTATION_MOVE_WEIGHT * mouseMoveDelta.x;
}
