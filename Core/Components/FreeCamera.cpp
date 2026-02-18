#include "stdafx.h"
#include "FreeCamera.h"
#include "Transform.h"
#include "Utils/InputEvents.h"
#include "Foundation/Entity.h"
using namespace Core;

Core::FreeCamera::FreeCamera()
	: _position(0.0f)
	, _yaw(0.0f)
	, _pitch(0.0f)
	, _initialized(false)
	, _moveSpeed(5.0f)
	, _fastMultiplier(3.0f)
	, _slowMultiplier(0.33f)
	, _mouseSensitivity(0.15f)
{
}

void Core::FreeCamera::UpdateFrame(float deltaTime)
{
	auto& transform = _entity->GetComponent<Transform>();

	// First frame: extract camera world position and orientation from existing view matrix
	if (!_initialized)
	{
		glm::mat4 viewMatrix = transform.GetMatrix();
		glm::mat4 invView = glm::inverse(viewMatrix);

		// Camera world position = column 3 of inverse view
		_position = glm::vec3(invView[3]);

		// Camera forward direction = -Z column of inverse view
		glm::vec3 forward = -glm::normalize(glm::vec3(invView[2]));

		// Derive yaw and pitch from forward vector
		_yaw = glm::degrees(atan2(forward.x, forward.z));
		_pitch = glm::degrees(asin(glm::clamp(forward.y, -1.0f, 1.0f)));

		_initialized = true;
	}

	HandleRotation();
	HandleMovement(deltaTime);

	// Build view matrix from camera world position and orientation
	// Unity-style: yaw around world Y, then pitch around local X
	// Forward = direction camera looks at
	float yawRad = glm::radians(_yaw);
	float pitchRad = glm::radians(_pitch);

	glm::vec3 forward;
	forward.x = sin(yawRad) * cos(pitchRad);
	forward.y = sin(pitchRad);
	forward.z = cos(yawRad) * cos(pitchRad);
	forward = glm::normalize(forward);

	glm::vec3 target = _position + forward;
	glm::mat4 viewMatrix = glm::lookAt(_position, target, glm::vec3(0.0f, 1.0f, 0.0f));

	// Decompose and set back to transform so the rest of the engine works
	transform.SetMatrix(viewMatrix);
}

void Core::FreeCamera::HandleRotation()
{
	auto& mouseButtonPressed = Core::Input::MouseButtonPressed;

	if (!mouseButtonPressed[MouseButton::Right])
		return;

	auto& mouseMoveDelta = Core::Input::MouseMoveDelta;

	_yaw -= _mouseSensitivity * mouseMoveDelta.x;
	_pitch -= _mouseSensitivity * mouseMoveDelta.y;

	// Clamp pitch to prevent flipping
	_pitch = glm::clamp(_pitch, -89.0f, 89.0f);
}

void Core::FreeCamera::HandleMovement(float deltaTime)
{
	auto& keyPressed = Core::Input::KeyPressed;
	auto& mouseButtonPressed = Core::Input::MouseButtonPressed;

	// Unity editor: WASDQE only works while right mouse button is held
	if (!mouseButtonPressed[MouseButton::Right])
		return;

	// Calculate camera axes from current yaw/pitch
	float yawRad = glm::radians(_yaw);
	float pitchRad = glm::radians(_pitch);

	glm::vec3 forward;
	forward.x = sin(yawRad) * cos(pitchRad);
	forward.y = sin(pitchRad);
	forward.z = cos(yawRad) * cos(pitchRad);
	forward = glm::normalize(forward);

	glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
	glm::vec3 up = glm::normalize(glm::cross(right, forward));

	// Accumulate movement direction
	glm::vec3 moveDir(0.0f);

	if (keyPressed[KeyCode::W]) moveDir += forward;
	if (keyPressed[KeyCode::S]) moveDir -= forward;
	if (keyPressed[KeyCode::D]) moveDir += right;
	if (keyPressed[KeyCode::A]) moveDir -= right;
	if (keyPressed[KeyCode::E]) moveDir += worldUp;
	if (keyPressed[KeyCode::Q]) moveDir -= worldUp;

	if (glm::length(moveDir) < 0.001f)
		return;

	moveDir = glm::normalize(moveDir);

	// Speed multiplier
	float speed = _moveSpeed;
	if (keyPressed[KeyCode::LeftShift])
		speed *= _fastMultiplier;
	if (keyPressed[KeyCode::LeftControl])
		speed *= _slowMultiplier;

	_position += moveDir * speed * deltaTime;
}

void Core::FreeCamera::Resize(uint32_t width, uint32_t height)
{
}

type_index Core::FreeCamera::GetType()
{
	return typeid(FreeCamera);
}
