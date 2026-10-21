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

		// World-space camera state; the entity transform holds the view matrix.
		const glm::vec3& GetPosition() const { return _position; }
		float GetYaw() const { return _yaw; }
		float GetPitch() const { return _pitch; }
		glm::vec3 GetForward() const;
		void SetPosition(const glm::vec3& position);
		void SetYawPitch(float yaw, float pitch);

	private:
		void HandleMovement(float deltaTime);
		void HandleRotation();
		void WriteViewMatrix();

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

