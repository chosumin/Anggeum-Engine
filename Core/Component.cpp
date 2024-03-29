#include "stdafx.h"
#include "Component.h"
#include "Entity.h"

Core::Component::Component(Entity& entity)
	:_entity(entity)
{
}