#pragma once
#include "Component.h"

namespace Core
{
	class Transform : public Component
	{
	public:
		Transform(Entity& entity);

		virtual ~Transform() = default;

		virtual void UpdateFrame(float deltaTime) override;
		virtual type_index GetType() override;
		virtual void Resize(uint32_t width, uint32_t height) override;

		void Rotate(const glm::vec3& deltaAngle);
		void Translate(const glm::vec4& delta);

		void SetTranslation(const glm::vec3& translation);
		void SetRotation(const glm::quat& rotation);
		void SetScale(const glm::vec3& scale);
		const vec3& GetTranslation() const;
		const quat& GetRotation() const;
		const vec3& GetScale() const;
		void SetMatrix(const glm::mat4& matrix);
		mat4 GetMatrix() const;
		//glm::mat4 GetWorldMatrix();

		/**
		 * @brief Marks the world transform invalid if any of
		 *        the local transform are changed or the parent
		 *        world transform has changed.
		 */
		void InvalidateWorldMatrix();

	private:
		//void UpdateWorldTransform();
	private:
		vec3 _translation = glm::vec3(0.0, 0.0, 0.0);
		quat _rotation = glm::quat(1.0, 0.0, 0.0, 0.0);
		vec3 _scale = glm::vec3(1.0, 1.0, 1.0);
		mat4 _worldMatrix = glm::mat4(1.0);

		bool _updateWorldMatrix = false;
	};
}

