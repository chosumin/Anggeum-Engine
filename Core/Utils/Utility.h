#pragma once

namespace Core
{
	class Utility
	{
	public:
		//hack : need abstract as Texture
		static bool HasStencilComponent(VkFormat format);

		static constexpr uint32_t HashCode(const char* str)
		{
			return str[0] ? static_cast<uint32_t>(str[0]) + 0xEDB8832Full * HashCode(str + 1) : 8603;
		}

		template <typename T>
		static inline std::vector<uint8_t> ToBytes(const T& value)
		{
			return std::vector<uint8_t>{reinterpret_cast<const uint8_t*>(&value),
				reinterpret_cast<const uint8_t*>(&value) + sizeof(T)};
		}

		template <class T>
		static inline uint32_t ToU32(T value)
		{
			static_assert(is_arithmetic<T>::value, "T must be numeric");

			if (static_cast<uintmax_t>(value) > static_cast<uintmax_t>(numeric_limits<uint32_t>::max()))
			{
				throw runtime_error("to_u32() failed, value is too big to be converted to uint32_t");
			}

			return static_cast<uint32_t>(value);
		}

		static void ConvertLowerCase(const string& src, string& dst)
		{
			transform(src.begin(), src.end(), dst.begin(), 
			[](unsigned char c) 
			{
				return tolower(c);
			});
		}
	};
}

