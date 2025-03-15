#pragma once

namespace Core
{
	class Entity;
	class Component
	{
	public:
		Component() = default;
		Component(Component&& other) = default;
		virtual ~Component() = default;

		virtual void UpdateFrame(float deltaTime) = 0;
		virtual std::type_index GetType() = 0;
		virtual void Resize(uint32_t width, uint32_t height) = 0;

		Entity& GetEntity() { return *_entity; }
		void SetEntity(Entity* entity) { _entity = entity; }
	protected:
		Entity* _entity = nullptr;
	};
}

