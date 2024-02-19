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

	class ModelPushConstant : public IPushConstant
	{
	public:
		ModelPushConstant();

		virtual uint32_t GetSize() const;
		virtual VkPushConstantRange GetPushConstantRange() const override;
		virtual uint32_t GetOffset() const;
		virtual void SetOffset(uint32_t offset);
	public:
		mat4 Matrix;
	private:
		uint32_t _offset;
	};
}

