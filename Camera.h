#pragma once

namespace Core
{
	class InputEvent;
	class PerspectiveCamera;
	class FreeCamera;
	class UniformBuffer;
	class Transform;
	class Camera
	{
	public:
		Camera(float width, float height);
		~Camera();

		void UpdateFrame(float deltaTime);
		void InputEvent(const Core::InputEvent& inputEvent);
		UniformBuffer* GetUniformBuffer() const;
	private:
		unique_ptr<PerspectiveCamera> _perspectiveCamera;
		unique_ptr<FreeCamera> _freeCamera;
		shared_ptr<Transform> _transform;
	};
}