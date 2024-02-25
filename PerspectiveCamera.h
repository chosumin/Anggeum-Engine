#pragma once
#include "UniformBuffer.h"
#include "Component.h"

namespace Core
{
	class PerspectiveCamera : public Component
	{
	public:
		PerspectiveCamera(Entity& entity, float width, float height);
		virtual ~PerspectiveCamera() = default;

		virtual type_index GetType() override;
		virtual void UpdateFrame(float deltaTime) override;
		virtual void Resize(uint32_t width, uint32_t height) override;

		void SetAspectRatio(float aspectRatio);
		void SetFieldOfView(float fov);
		float GetFarPlane() const;
		void SetFarPlane(float zfar);
		float GetNearPlane() const;
		void SetNearPlane(float znear);
		float GetAspectRatio();
		float GetFieldOfView();
		mat4 GetProjection();
		mat4 GetView();
		const mat4 GetPreRotation();
		void SetPreRotation(const glm::mat4& pre_rotation);
	private:
		void MapBufferMemory(void* uniformBufferMapped) override;
	private:
		VPBufferObject _matrices;
	private:
		float _aspectRatio{ 1.0f };
		float _fov{ glm::radians(60.0f) };
		float _farPlane{ 100.0 };
		float _nearPlane{ 0.1f };

		glm::mat4 _preRotation{ 1.0f };
	};
}

