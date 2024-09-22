#pragma once
#include "Components/Transform.h"

namespace Core
{
	class Component;
	class Entity
	{
	public:
		Entity(const size_t id, const string& name);
		virtual ~Entity();

		const size_t GetId() const;
		const string& GetName() const;

		Transform& GetTransform();

		void SetParent(Entity& parent);
		Entity* GetParent() const;
		void AddChild(Entity& child);
		const vector<Entity*>& GetChildren() const;
		void SetComponent(Component& component);

		template <class T>
		inline T& GetComponent()
		{
			return dynamic_cast<T&>(GetComponent(typeid(T)));
		}

		Component& GetComponent(const type_index index);

		template <class T>
		bool HasComponent()
		{
			return HasComponent(typeid(T));
		}

		bool HasComponent(const type_index index);

	private:
		size_t _id;
		string _name;
		Transform _transform;
		Entity* _parent{ nullptr };
		vector<Entity*> _children;
		unordered_map<type_index, Component*> _components;
	};
}

