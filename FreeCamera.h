#pragma once

namespace Core
{
	class InputEvent;
	class Transform;
	class FreeCamera
	{
	public:
		static const float TOUCH_DOWN_MOVE_FORWARD_WAIT_TIME;
		static const float ROTATION_MOVE_WEIGHT;
		static const float KEY_ROTATION_MOVE_WEIGHT;
		static const float TRANSLATION_MOVE_WEIGHT;
		static const float TRANSLATION_MOVE_STEP;
		static const uint32_t TRANSLATION_MOVE_SPEED;
	public:
		FreeCamera(weak_ptr<Transform> transform);
		void UpdateFrame(float deltaTime);
		void InputEvent(const Core::InputEvent& inputEvent);
		void Resize(uint32_t width, uint32_t height);
	private:
		float _speedMultiplier{ 3.0f };
		unordered_map<KeyCode, bool> _keyPressed;
		unordered_map<MouseButton, bool> _mouseButtonPressed;
		glm::vec2 _mouseMoveDelta{ 0.0f };
		glm::vec2 _mouseLastPos{ 0.0f };

		weak_ptr<Transform> _transform;
	};
}

