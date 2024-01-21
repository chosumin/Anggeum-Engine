#pragma once
#include "UniformBuffer.h"

namespace Core
{
	class Transform;
	class PerspectiveCamera : public UniformBuffer
	{
	public:
		PerspectiveCamera(const std::string& name, 
			float width, float height, weak_ptr<Transform> transform);
		virtual ~PerspectiveCamera() = default;

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
		struct VPBufferObject
		{
			alignas(16) mat4 View;
			alignas(16) mat4 Perspective;
		} _matrices;
	private:
		float _aspectRatio{ 1.0f };
		float _fov{ glm::radians(60.0f) };
		float _farPlane{ 100.0 };
		float _nearPlane{ 0.1f };

		glm::mat4 _preRotation{ 1.0f };

		weak_ptr<Transform> _transform;
	};
}

