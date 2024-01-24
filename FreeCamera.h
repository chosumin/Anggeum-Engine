#pragma once
#include "Component.h"

namespace Core
{
	class FreeCamera : public Component
	{
	public:
		static const float TOUCH_DOWN_MOVE_FORWARD_WAIT_TIME;
		static const float ROTATION_MOVE_WEIGHT;
		static const float KEY_ROTATION_MOVE_WEIGHT;
		static const float TRANSLATION_MOVE_WEIGHT;
		static const float TRANSLATION_MOVE_STEP;
		static const uint32_t TRANSLATION_MOVE_SPEED;
	public:
		FreeCamera(Entity& entity);
		virtual ~FreeCamera() = default;

		virtual void UpdateFrame(float deltaTime) override;
		virtual void Resize(uint32_t width, uint32_t height) override;
		virtual type_index GetType() override;
	private:
		float _speedMultiplier{ 3.0f };
	};
}

