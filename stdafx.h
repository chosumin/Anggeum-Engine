#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
using namespace glm;

#include <chrono>
#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>
#include <memory>
#include <string>
#include <stdexcept>
#include <map>
#include <optional>
#include <set>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <array>
#include <unordered_map>
#include <typeindex>
#include <typeinfo>
#include <assert.h>
#include <queue>
using namespace std;

#include "Device.h"
#include "Window.h"
#include "InputEvents.h"

const int MAX_FRAMES_IN_FLIGHT = 2;