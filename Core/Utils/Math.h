#pragma once

namespace Core
{
	class Math
	{
	public:
		static float ToRadian(float angle)
		{
			return angle * pi<float>() / 180.0f;
		}
	};
}
