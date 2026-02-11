# AnggeumEngine

Vulkan-based rendering engine

## Requirements

- **CMake** 3.20 or higher
- **Visual Studio 2022** (MSVC compiler)
- **Vulkan SDK** 1.3.275.0 or higher
- **Windows 10/11** (64-bit)

## Build Instructions

### 1. Install Vulkan SDK

Download and install from [Vulkan SDK](https://vulkan.lunarg.com/sdk/home), or set the `VULKAN_SDK` environment variable:

```cmd
set VULKAN_SDK=C:\VulkanSDK\1.3.275.0
```

### 2. Generate CMake Project

```cmd
cmake -B Build
```

### 3. Open in Visual Studio

```cmd
Build\AnggeumEngine.sln
```

Or open CMakeLists.txt directly in Visual Studio:
- `File` → `Open` → `CMake...` → Select `CMakeLists.txt`

### 4. Build and Run

- **Debug Mode**: Press F5 or `Debug` → `Start Debugging`
- **Release Mode**: Change build configuration to Release and run

## Project Structure

```
AnggeumEngine/
├── CMakeLists.txt          # CMake build configuration
├── main.cpp                # Main entry point
├── Application.h/cpp       # Application class
├── stdafx.h/cpp            # Precompiled Header
├── Assets/                 # Resources (shaders, textures)
│   ├── Shaders/
│   └── Textures/
├── Core/                   # Engine core code
│   ├── Graphics/
│   ├── Vulkan/
│   └── ...
├── Sample/                 # Sample code
└── ThirdParties/           # Third-party libraries
    ├── glfw-3.3.8.bin.WIN64/
    ├── glm/
    ├── imgui/
    ├── ktx/
    ├── stb/
    ├── tinygltf/
    └── rapidjson/
```

## Third-Party Libraries

- **GLFW** 3.3.8 - Window and input handling
- **GLM** - Mathematics library
- **ImGui** - GUI library
- **KTX** - Texture compression
- **STB** - Image loading
- **TinyGLTF** - glTF model loading
- **RapidJSON** - JSON parsing

## Troubleshooting

### Cannot find Vulkan Validation Layers

Check environment variable:

```cmd
echo %VULKAN_SDK%
```

If not set:

```cmd
setx VULKAN_SDK "C:\VulkanSDK\1.3.275.0"
```

Restart Visual Studio required.

### Missing DLLs

After building, the following DLLs should be automatically copied to the executable directory:
- `ktx.dll`
- `shaderc_shared.dll`
- `spirv-cross-c-shared.dll`

If auto-copy fails, regenerate CMake:

```cmd
cmake -B Build --fresh
```

### GLFW Library Not Found

If you see `glfw3.lib not found`, check the GLFW library path in CMakeLists.txt and change `lib-vc2022` to match your actual folder name (e.g., `lib-vc2019`).

## Development

### Debug Mode

Debug mode includes:
- Runtime checks for uninitialized variables
- Vulkan validation layers enabled
- Full debug symbols

### Release Mode

Release mode includes:
- Full optimization
- No runtime checks (may expose uninitialized variable bugs)
- Better performance

### RelWithDebInfo Mode (Recommended for Testing)

Combines optimization with debug information - useful for finding Release-only bugs.

## Known Issues

- Release mode may crash if variables are not properly initialized
- Vulkan validation layers require proper `VULKAN_SDK` environment variable
- KTX library only has Release DLL (Debug builds use Release DLL)

## License

MIT License

Copyright (c) 2026 chosumin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.