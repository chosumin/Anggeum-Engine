#pragma once
#include "UniformBuffer.h"

namespace Core
{
	struct VPBufferObject
	{
		alignas(16) mat4 View;
		alignas(16) mat4 Perspective;
	};
}

