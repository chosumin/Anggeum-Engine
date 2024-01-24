#include "stdafx.h"
#include "Scene.h"
#include "Component.h"

Core::Scene::Scene(const string& name)
	:_name(name)
{
}

void Core::Scene::SetName(const string& name)
{
	_name = name;
}

const string& Core::Scene::GetName() const
{
	return _name;
}

void Core::Scene::SetEntities(vector<unique_ptr<Entity>>&& entities)
{
	assert(entities.empty() && "Scene nodes were already set");
	_entities = std::move(entities);
}

void Core::Scene::AddEntity(unique_ptr<Entity>&& entity)
{
	_root->AddChild(*entity);
	_entities.emplace_back(std::move(entity));
}

void Core::Scene::AddComponent(unique_ptr<Component>&& component)
{
	if (component)
	{
		_components[component->GetType()].push_back(std::move(component));
	}
}

void Core::Scene::AddComponent(unique_ptr<Component>&& component, Entity& entity)
{
	entity.SetComponent(*component);

	if (component)
	{
		_components[component->GetType()].push_back(std::move(component));
	}
}

void Core::Scene::SetComponents(const type_index& type_info, vector<unique_ptr<Component>>&& components)
{
	_components[type_info] = std::move(components);
}

const vector<unique_ptr<Core::Component>>& Core::Scene::GetComponents(const type_index& type_info) const
{
	return _components.at(type_info);
}

bool Core::Scene::HasComponent(const type_index& type_info) const
{
	auto component = _components.find(type_info);
	return (component != _components.end() && !component->second.empty());
}

Core::Entity* Core::Scene::FindEntity(const string& name)
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

void Core::Scene::SetRootEntity(Entity& entity)
{
	_root = &entity;
}

Core::Entity& Core::Scene::GetRootEntity()
{
	return *_root;
}
