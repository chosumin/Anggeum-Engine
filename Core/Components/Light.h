#pragma once
#include "Foundation/Component.h"

namespace Core
{
	enum LightType
	{
		Directional = 0,
		Point = 1,
		Spot = 2,
		Max
	};

	struct LightProperties
	{
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
		float Intensity{ 1.0f };
		float Range{ 0.0f };
		float InnerConeAngle{ 0.0f };
		float OuterConeAngle{ 0.0f };
	};

	class Light : public Component
	{
	public:
		Light(const std::string& name);
		Light(Light&& other) = default;

		virtual ~Light() = default;

		// Component을(를) 통해 상속됨
		type_index GetType() override;
		void UpdateFrame(float deltaTime) override;
		void Resize(uint32_t width, uint32_t height) override;

		const LightType& GetLightType();
		void SetLightType(const LightType& type);

		LightProperties& GetProperties();
		void SetProperties(const LightProperties& properties);
	private:
		string _name;
		LightType _lightType;
		LightProperties _properties;
	};
}