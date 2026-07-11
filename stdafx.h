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
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/type_ptr.hpp>
using namespace glm;

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"
using namespace rapidjson;

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

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
#include <unordered_set>
#include <typeindex>
#include <typeinfo>
#include <assert.h>
#include <queue>
#include <mutex>
#include <thread>
#include <sstream>
#include <random>
#include <bitset>
#include <filesystem>
#include <deque>
using namespace std;

#include "Graphics/Vulkans/Device.h"
#include "Foundation/Window.h"
#include "Utils/InputEvents.h"

const int MAX_FRAMES_IN_FLIGHT = 2;