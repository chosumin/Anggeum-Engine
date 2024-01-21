#pragma once

namespace Core
{
	class Transform
	{
	public:
		Transform();

		virtual ~Transform() = default;

		std::type_index GetType();

		void SetTranslation(const glm::vec3& translation);
		void SetRotation(const glm::quat& rotation);
		void SetScale(const glm::vec3& scale);
		const glm::vec3& GetTranslation() const;
		const glm::quat& GetRotation() const;
		const glm::vec3& GetScale() const;
		void SetMatrix(const glm::mat4& matrix);
		glm::mat4 GetMatrix() const;
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
		glm::vec3 _translation = glm::vec3(0.0, 0.0, 0.0);
		glm::quat _rotation = glm::quat(1.0, 0.0, 0.0, 0.0);
		glm::vec3 _scale = glm::vec3(1.0, 1.0, 1.0);
		glm::mat4 _worldMatrix = glm::mat4(1.0);

		bool _updateWorldMatrix = false;
	};
}

