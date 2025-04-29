#pragma once

namespace Core
{
	class CommandBuffer;
	class AsyncLoadable
	{
	public:
		virtual void Load(CommandBuffer& commandBuffer) = 0;
	};
}