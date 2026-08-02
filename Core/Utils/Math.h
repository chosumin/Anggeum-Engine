#pragma once

namespace Core
{
	class Math
	{
	public:
		static void ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes);
	};
}
