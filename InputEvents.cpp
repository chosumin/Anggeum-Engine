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
