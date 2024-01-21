#include "stdafx.h"
#include "FreeCamera.h"
#include "Transform.h"

const float Core::FreeCamera::TOUCH_DOWN_MOVE_FORWARD_WAIT_TIME = 2.0f;
const float Core::FreeCamera::ROTATION_MOVE_WEIGHT = 1.0f;
const float Core::FreeCamera::KEY_ROTATION_MOVE_WEIGHT = 0.5f;
const float Core::FreeCamera::TRANSLATION_MOVE_WEIGHT = 3.0f;
const float Core::FreeCamera::TRANSLATION_MOVE_STEP = 5.0f;
const uint32_t Core::FreeCamera::TRANSLATION_MOVE_SPEED = 3;

Core::FreeCamera::FreeCamera(weak_ptr<Transform> transform)
{
	_transform = transform;
}

void Core::FreeCamera::UpdateFrame(float deltaTime)
{
	glm::vec3 deltaTranslation(0.0f, 0.0f, 0.0f);
	glm::vec3 deltaRotation(0.0f, 0.0f, 0.0f);

	float mulTranslation = _speedMultiplier;

	if (_keyPressed[KeyCode::W])
	{
		deltaTranslation.z -= TRANSLATION_MOVE_STEP;
	}
	if (_keyPressed[KeyCode::S])
	{
		deltaTranslation.z += TRANSLATION_MOVE_STEP;
	}
	if (_keyPressed[KeyCode::A])
	{
		deltaTranslation.x -= TRANSLATION_MOVE_STEP;
	}
	if (_keyPressed[KeyCode::D])
	{
		deltaTranslation.x += TRANSLATION_MOVE_STEP;
	}
	if (_keyPressed[KeyCode::Q])
	{
		deltaTranslation.y -= TRANSLATION_MOVE_STEP;
	}
	if (_keyPressed[KeyCode::E])
	{
		deltaTranslation.y += TRANSLATION_MOVE_STEP;
	}
	if (_keyPressed[KeyCode::LeftControl])
	{
		mulTranslation *= (1.0f * TRANSLATION_MOVE_SPEED);
	}
	if (_keyPressed[KeyCode::LeftShift])
	{
		mulTranslation *= (1.0f / TRANSLATION_MOVE_SPEED);
	}

	if (_keyPressed[KeyCode::I])
	{
		deltaRotation.x += KEY_ROTATION_MOVE_WEIGHT;
	}
	if (_keyPressed[KeyCode::K])
	{
		deltaRotation.x -= KEY_ROTATION_MOVE_WEIGHT;
	}
	if (_keyPressed[KeyCode::J])
	{
		deltaRotation.y += KEY_ROTATION_MOVE_WEIGHT;
	}
	if (_keyPressed[KeyCode::L])
	{
		deltaRotation.y -= KEY_ROTATION_MOVE_WEIGHT;
	}

	if (_mouseButtonPressed[MouseButton::Left] && _mouseButtonPressed[MouseButton::Right])
	{
		deltaRotation.z += TRANSLATION_MOVE_WEIGHT * _mouseMoveDelta.x;
	}
	else if (_mouseButtonPressed[MouseButton::Right])
	{
		deltaRotation.x -= ROTATION_MOVE_WEIGHT * _mouseMoveDelta.y;
		deltaRotation.y -= ROTATION_MOVE_WEIGHT * _mouseMoveDelta.x;
	}
	else if (_mouseButtonPressed[MouseButton::Left])
	{
		deltaTranslation.x += TRANSLATION_MOVE_WEIGHT * _mouseMoveDelta.x;
		deltaTranslation.y += TRANSLATION_MOVE_WEIGHT * -_mouseMoveDelta.y;
	}

	deltaTranslation *= mulTranslation * deltaTime;
	deltaRotation *= deltaTime;

	// Only re-calculate the transform if it's changed
	if (deltaRotation != glm::vec3(0.0f, 0.0f, 0.0f) || deltaTranslation != glm::vec3(0.0f, 0.0f, 0.0f))
	{
		//auto& transform = get_node().get_component<Transform>();
		auto transform = _transform.lock();

		glm::quat qx = glm::angleAxis(deltaRotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::quat qy = glm::angleAxis(deltaRotation.y, glm::vec3(0.0f, 1.0f, 0.0f));

		glm::quat orientation = glm::normalize(qy * transform->GetRotation() * qx);

		transform->SetTranslation(transform->GetTranslation() + deltaTranslation * glm::conjugate(orientation));
		transform->SetRotation(orientation);
	}

	_mouseMoveDelta = {};
}

void Core::FreeCamera::InputEvent(const Core::InputEvent& inputEvent)
{
	if (inputEvent.GetSource() == EventSource::Keyboard)
	{
		const auto& key_event = static_cast<const KeyInputEvent&>(inputEvent);

		if (key_event.GetAction() == KeyAction::Down ||
			key_event.GetAction() == KeyAction::Repeat)
		{
			_keyPressed[key_event.GetCode()] = true;
		}
		else
		{
			_keyPressed[key_event.GetCode()] = false;
		}
	}
	else if (inputEvent.GetSource() == EventSource::Mouse)
	{
		const auto& mouse_button = static_cast<const MouseButtonInputEvent&>(inputEvent);

		glm::vec2 mouse_pos{ floor(mouse_button.GetPosX()), floor(mouse_button.GetPosY()) };

		if (mouse_button.GetAction() == MouseAction::Down)
		{
			_mouseButtonPressed[mouse_button.GetButton()] = true;
		}

		if (mouse_button.GetAction() == MouseAction::Up)
		{
			_mouseButtonPressed[mouse_button.GetButton()] = false;
		}

		if (mouse_button.GetAction() == MouseAction::Move)
		{
			_mouseMoveDelta = mouse_pos - _mouseLastPos;
			_mouseLastPos = mouse_pos;
		}
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
