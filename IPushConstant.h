#pragma once

namespace Core
{
	class IPushConstant
	{
	public:
		virtual uint32_t GetSize() const = 0;
		virtual VkPushConstantRange GetPushConstantRange() const = 0;
		virtual uint32_t GetOffset() const = 0;
		virtual void SetOffset(uint32_t offset) = 0;
	};
}

