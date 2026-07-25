#pragma once

namespace Core
{
	template<typename T>
	class ResourcePool;

	/*
	 * Generational reference to a resource owned by a ResourcePool.
	 *
	 * Phantom-typed: Handle<Texture> and Handle<Shader> are unrelated types, so a
	 * call site cannot pass one where the other is expected.
	 *
	 * Carries a non-owning back-pointer to its pool so Get() resolves without the
	 * caller needing the cache. Identity is (index, generation) only; the pool
	 * pointer is an accessor, not part of equality or the hash. A serialized handle
	 * would keep just those two fields and rebind the pool on load.
	 *
	 * This is a CPU-side cache identifier and is deliberately NOT the same thing as
	 * BindlessTextureManager's TextureHandle, which indexes a GPU descriptor array.
	 * The two may be unified later; until then a texture legitimately has both, and
	 * they must not be assigned to one another.
	 */
	template<typename T>
	struct Handle
	{
		static constexpr uint32_t InvalidIndex = UINT32_MAX;

		ResourcePool<T>* pool = nullptr;
		uint32_t index = InvalidIndex;
		uint32_t generation = 0;

		bool IsValid() const { return index != InvalidIndex; }

		// nullptr if default-constructed or stale (the slot was freed/reused).
		T* TryGet() const;
		// Asserts the handle still resolves; use TryGet() when absence is expected.
		T& Get() const;
		// Shared ownership of the pooled resource, for APIs that still take a
		// shared_ptr (e.g. binding a texture alongside shared_ptr render targets).
		shared_ptr<T> GetShared() const;

		bool operator==(const Handle& other) const
		{
			return index == other.index && generation == other.generation;
		}
		bool operator!=(const Handle& other) const { return !(*this == other); }
	};
}

namespace std
{
	template<typename T>
	struct hash<Core::Handle<T>>
	{
		size_t operator()(const Core::Handle<T>& handle) const noexcept
		{
			return (static_cast<size_t>(handle.generation) << 32) ^ handle.index;
		}
	};
}
