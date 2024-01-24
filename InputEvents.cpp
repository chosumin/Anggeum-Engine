#include "stdafx.h"
#include "InputEvents.h"

Core::InputEvent::InputEvent(EventSource source)
    :source{ source }
{
}

Core::EventSource Core::InputEvent::GetSource() const
{
    return source;
}

Core::KeyInputEvent::KeyInputEvent(KeyCode code, KeyAction action)
    :InputEvent{ EventSource::Keyboard },
    code{ code },
    action{ action }
{
}

Core::KeyCode Core::KeyInputEvent::GetCode() const
{
    return code;
}

Core::KeyAction Core::KeyInputEvent::GetAction() const
{
    return action;
}

Core::MouseButtonInputEvent::MouseButtonInputEvent(MouseButton button, MouseAction action, float pos_x, float pos_y)
    :InputEvent{ EventSource::Mouse },
    button{ button },
    action{ action },
    pos_x{ pos_x },
    pos_y{ pos_y }
{
}

Core::MouseButton Core::MouseButtonInputEvent::GetButton() const
{
    return button;
}

Core::MouseAction Core::MouseButtonInputEvent::GetAction() const
{
    return action;
}

float Core::MouseButtonInputEvent::GetPosX() const
{
    return pos_x;
}

float Core::MouseButtonInputEvent::GetPosY() const
{
    return pos_y;
}

unordered_map<Core::KeyCode, bool> Core::Input::KeyPressed = {};
unordered_map<Core::MouseButton, bool> Core::Input::MouseButtonPressed = {};
vec2 Core::Input::MouseMoveDelta = {};
vec2 Core::Input::MouseLastPos = {};

void Core::Input::InputEvent(const Core::InputEvent& inputEvent)
{
	if (inputEvent.GetSource() == EventSource::Keyboard)
	{
		const auto& key_event = static_cast<const KeyInputEvent&>(inputEvent);

		if (key_event.GetAction() == KeyAction::Down ||
			key_event.GetAction() == KeyAction::Repeat)
		{
			KeyPressed[key_event.GetCode()] = true;
		}
		else
		{
			KeyPressed[key_event.GetCode()] = false;
		}
	}
	else if (inputEvent.GetSource() == EventSource::Mouse)
	{
		const auto& mouse_button = static_cast<const MouseButtonInputEvent&>(inputEvent);

		MouseMoveDelta = {};

		glm::vec2 mouse_pos{ floor(mouse_button.GetPosX()), floor(mouse_button.GetPosY()) };

		if (mouse_button.GetAction() == MouseAction::Down)
		{
			MouseButtonPressed[mouse_button.GetButton()] = true;
		}

		if (mouse_button.GetAction() == MouseAction::Up)
		{
			MouseButtonPressed[mouse_button.GetButton()] = false;
		}

		if (mouse_button.GetAction() == MouseAction::Move)
		{
			MouseMoveDelta = mouse_pos - MouseLastPos;
			MouseLastPos = mouse_pos;
		}
	}
}
