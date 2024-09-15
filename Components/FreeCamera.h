#pragma once
#include "Component.h"

namespace Core
{
	class FreeCamera : public Component
	{
	public:
		FreeCamera(Entity& entity);
		virtual ~FreeCamera() = default;

		virtual void UpdateFrame(float deltaTime) override;
		virtual void Resize(uint32_t width, uint32_t height) override;
		virtual type_index GetType() override;
	private:
		void Translate(vec3& delta, float& multiplier);
	};
}

