#pragma once
#include "Foundation/Component.h"

namespace Core
{
	class FreeCamera : public Component
	{
	public:
		FreeCamera();
		virtual ~FreeCamera() = default;

		virtual void UpdateFrame(float deltaTime) override;
		virtual void Resize(uint32_t width, uint32_t height) override;
		virtual type_index GetType() override;

	private:
		void HandleMovement(float deltaTime);
		void HandleRotation();

		// Camera world-space position and orientation
		glm::vec3 _position;
		float _yaw;    // Rotation around world Y axis (degrees)
		float _pitch;  // Rotation around local X axis (degrees)

		bool _initialized;

		// Settings
		float _moveSpeed;
		float _fastMultiplier;
		float _slowMultiplier;
		float _mouseSensitivity;
	};
}

