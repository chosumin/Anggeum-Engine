#pragma once
#include "Entity.h"

namespace Core
{
	//class Entity;
	class Component;
	class Scene
	{
	public:
		Scene() = default;
		Scene(const string& name);

		void SetName(const string& name);
		const string& GetName() const;

		void SetEntities(vector<unique_ptr<Entity>>&& entities);
		void AddEntity(unique_ptr<Entity>&& entity);

		void AddComponent(unique_ptr<Component>&& component);
		void AddComponent(unique_ptr<Component>&& component, Entity& entity);

		/**
		 * @brief Set list of components for the given type
		 * @param type_info The type of the component
		 * @param components The list of components (retained)
		 */
		void SetComponents(const type_index& type_info, vector<unique_ptr<Component>>&& components);

		/**
		 * @brief Set list of components casted from the given template type
		 */
		template <class T>
		void SetComponents(vector<unique_ptr<T>>&& components)
		{
			vector<unique_ptr<Component>> result(components.size());
			transform(components.begin(), components.end(), result.begin(),
				[](unique_ptr<T>& component) -> unique_ptr<Component> {
					return unique_ptr<Component>(move(component));
				});
			SetComponents(typeid(T), move(result));
		}

		/**
		 * @brief Clears a list of components
		 */
		template <class T>
		void ClearComponents()
		{
			SetComponents(typeid(T), {});
		}

		/**
		 * @return List of pointers to components casted to the given template type
		 */
		template <class T>
		vector<T*> GetComponents() const
		{
			vector<T*> result;
			if (HasComponent(typeid(T)))
			{
				auto& scene_components = GetComponents(typeid(T));

				result.resize(scene_components.size());
				transform(scene_components.begin(), scene_components.end(), result.begin(),
					[](const unique_ptr<Component>& component) -> T* {
						return dynamic_cast<T*>(component.get());
					});
			}

			return result;
		}

		/**
		 * @return List of components for the given type
		 */
		const vector<unique_ptr<Component>>& GetComponents(const type_index& type_info) const;

		template <class T>
		bool HasComponent() const
		{
			return HasComponent(typeid(T));
		}

		bool HasComponent(const type_index& type_info) const;

		Entity* FindEntity(const string& name);
		void SetRootEntity(Entity& entity);
		Entity& GetRootEntity();

	private:
		string _name;
		vector<unique_ptr<Core::Entity>> _entities;
		Entity* _root{ nullptr };
		unordered_map<type_index, vector<unique_ptr<Component>>> _components;
	};
}

