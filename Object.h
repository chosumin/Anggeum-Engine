#pragma once

namespace Core
{
	class Component;
	class Transform;
	class Object
	{
	public:
		Object(const size_t id);
		virtual ~Object() = default;

		const size_t GetId() const;
		Transform& GetTransform();
		
	};
}

