#pragma once

struct VPBufferObject
{
	alignas(16) mat4 View;
	alignas(16) mat4 Perspective;
};