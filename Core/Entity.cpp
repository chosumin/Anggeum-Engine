#include "stdafx.h"
#include "Entity.h"
#include "Component.h"

Core::Entity::Entity(const size_t id, const string& name)
	:_id(id), _name(name), _transform{ *this }
{
	SetComponent(_transform);
}

Core::Entity::~Entity()
{
}

const size_t Core::Entity::GetId() const
{
	return _id;
}

const string& Core::Entity::GetName() const
{
	return _name;
}

Core::Transform& Core::Entity::GetTransform()
{
	return _transform;
}

void Core::Entity::SetParent(Entity& parent)
{
	_parent = &parent;
	_transform.InvalidateWorldMatrix();
}

Core::Entity* Core::Entity::GetParent() const
{
	return _parent;
}

void Core::Entity::AddChild(Entity& child)
{
	_children.push_back(&child);
}

const vector<Core::Entity*>& Core::Entity::GetChildren() const
{
	return _children;
}

void Core::Entity::SetComponent(Component& component)
{
	auto it = _components.find(component.GetType());

	if (it != _components.end())
	{
		it->second = &component;
	}
	else
	{
		_components.insert(make_pair(component.GetType(), &component));
	}
}

Core::Component& Core::Entity::GetComponent(const type_index index)
{
	return *_components.at(index);
}

bool Core::Entity::HasComponent(const type_index index)
{
	return _components.count(index) > 0;
}
