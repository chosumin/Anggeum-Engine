#include "stdafx.h"
#include "Foundation/Scene.h"
#include "Foundation/Component.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"

namespace Core
{
	Scene::Scene()
		:_name("Default")
	{
		_root = make_unique<Entity>(0, "root");
	}

	Scene::Scene(const string& name)
		:_name(name)
	{
	}

	Scene::~Scene()
	{
	}

	void Scene::SetName(const string& name)
	{
		_name = name;
	}

	const string& Scene::GetName() const
	{
		return _name;
	}

	void Scene::SetEntities(vector<unique_ptr<Entity>>&& entities)
	{
		assert(entities.empty() && "Scene nodes were already set");
		_entities = std::move(entities);
	}

	void Scene::AddEntity(unique_ptr<Entity>&& entity)
	{
		_root->AddChild(*entity);
		_entities.emplace_back(std::move(entity));
	}

	size_t Scene::AllocateEntityId()
	{
		if (!_freeEntityIds.empty())
		{
			size_t id = _freeEntityIds.back();
			_freeEntityIds.pop_back();
			return id;
		}

		return _nextEntityId++;
	}

	void Scene::RemoveEntity(Entity& entity)
	{
		// The Transform is a member of the entity, so it has no scene-owned entry.
		for (auto& [type, component] : entity.GetComponents())
		{
			auto owned = _components.find(type);
			if (owned == _components.end())
				continue;

			auto& list = owned->second;
			list.erase(std::remove_if(list.begin(), list.end(),
				[component](const unique_ptr<Component>& candidate)
				{
					return candidate.get() == component;
				}), list.end());
		}

		_root->RemoveChild(entity);

		// Only ids this scene handed out go back to the pool.
		if (entity.GetId() < _nextEntityId)
			_freeEntityIds.push_back(entity.GetId());

		_entities.erase(std::remove_if(_entities.begin(), _entities.end(),
			[&entity](const unique_ptr<Entity>& candidate)
			{
				return candidate.get() == &entity;
			}), _entities.end());
	}

	void Scene::AddComponent(unique_ptr<Component>&& component)
	{
		if (component)
		{
			_components[component->GetType()].push_back(move(component));
		}
	}

	void Scene::AddComponent(unique_ptr<Component>&& component, Entity& entity)
	{
		entity.SetComponent(*component);

		if (component)
		{
			_components[component->GetType()].push_back(std::move(component));
		}
	}

	void Scene::SetComponents(const type_index& typeInfo, vector<unique_ptr<Component>>&& components)
	{
		_components[typeInfo] = std::move(components);
	}

	const vector<unique_ptr<Component>>& Scene::GetComponents(const type_index& typeInfo) const
	{
		return _components.at(typeInfo);
	}

	bool Scene::HasComponent(const type_index& type_info) const
	{
		auto component = _components.find(type_info);
		return (component != _components.end() && !component->second.empty());
	}

	Entity* Scene::FindEntity(const string& name)
	{
		for (auto rootEntity : _root->GetChildren())
		{
			std::queue<Core::Entity*> traverseEntities{};
			traverseEntities.push(rootEntity);

			while (!traverseEntities.empty())
			{
				auto entity = traverseEntities.front();
				traverseEntities.pop();

				if (entity->GetName() == name)
				{
					return entity;
				}

				for (auto child_node : entity->GetChildren())
				{
					traverseEntities.push(child_node);
				}
			}
		}

		return nullptr;
	}

	Entity& Scene::GetRootEntity()
	{
		return *_root;
	}

	PerspectiveCamera* Scene::GetMainCamera() const
	{
		auto cameras = GetComponents<PerspectiveCamera>();

		if (cameras.empty())
			return nullptr;

		return cameras[0];
	}

	Light* Scene::GetMainLight() const
	{
		auto lights = GetComponents<Light>();

		if (lights.empty())
			return nullptr;

		return lights[0];
	}
}