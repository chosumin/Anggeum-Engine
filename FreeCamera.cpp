#include "stdafx.h"
#include "FreeCamera.h"
#include "Transform.h"
#include "InputEvents.h"
#include "Entity.h"
using namespace Core;

const float Core::FreeCamera::TOUCH_DOWN_MOVE_FORWARD_WAIT_TIME = 2.0f;
const float Core::FreeCamera::ROTATION_MOVE_WEIGHT = 1.0f;
const float Core::FreeCamera::KEY_ROTATION_MOVE_WEIGHT = 0.5f;
const float Core::FreeCamera::TRANSLATION_MOVE_WEIGHT = 3.0f;
const float Core::FreeCamera::TRANSLATION_MOVE_STEP = 5.0f;
const uint32_t Core::FreeCamera::TRANSLATION_MOVE_SPEED = 3;

Core::FreeCamera::FreeCamera(Entity& entity)
	:Component(entity)
{
}

void Core::FreeCamera::UpdateFrame(float deltaTime)
{
	glm::vec3 deltaTranslation(0.0f, 0.0f, 0.0f);
	glm::vec3 deltaRotation(0.0f, 0.0f, 0.0f);

	float mulTranslation = _speedMultiplier;

	auto& keyPressed = Core::Input::KeyPressed;
	if (keyPressed[KeyCode::W])
	{
		deltaTranslation.z -= TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::S])
	{
		deltaTranslation.z += TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::A])
	{
		deltaTranslation.x -= TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::D])
	{
		deltaTranslation.x += TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::Q])
	{
		deltaTranslation.y -= TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::E])
	{
		deltaTranslation.y += TRANSLATION_MOVE_STEP;
	}
	if (keyPressed[KeyCode::LeftControl])
	{
		mulTranslation *= (1.0f * TRANSLATION_MOVE_SPEED);
	}
	if (keyPressed[KeyCode::LeftShift])
	{
		mulTranslation *= (1.0f / TRANSLATION_MOVE_SPEED);
	}

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
	else if (mouseButtonPressed[MouseButton::Left])
	{
		deltaTranslation.x += TRANSLATION_MOVE_WEIGHT * mouseMoveDelta.x;
		deltaTranslation.y += TRANSLATION_MOVE_WEIGHT * -mouseMoveDelta.y;
	}

	deltaTranslation *= mulTranslation * deltaTime;
	deltaRotation *= deltaTime;

	// Only re-calculate the transform if it's changed
	if (deltaRotation != glm::vec3(0.0f, 0.0f, 0.0f) || deltaTranslation != glm::vec3(0.0f, 0.0f, 0.0f))
	{
		auto& transform = _entity.GetComponent<Transform>();
		
		glm::quat qx = glm::angleAxis(deltaRotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::quat qy = glm::angleAxis(deltaRotation.y, glm::vec3(0.0f, 1.0f, 0.0f));

		glm::quat orientation = glm::normalize(qy * transform.GetRotation() * qx);

		transform.SetTranslation(transform.GetTranslation() + deltaTranslation * glm::conjugate(orientation));
		transform.SetRotation(orientation);
	}
}

void Core::FreeCamera::Resize(uint32_t width, uint32_t height)
{
	/*auto& camera_node = get_node();

	if (camera_node.has_component<Camera>())
	{
		if (auto camera = dynamic_cast<PerspectiveCamera*>(&camera_node.get_component<Camera>()))
		{
			camera->set_aspect_ratio(static_cast<float>(width) / height);
		}
	}*/
}

type_index Core::FreeCamera::GetType()
{
	return typeid(FreeCamera);
}
