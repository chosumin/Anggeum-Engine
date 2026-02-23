# Angeum Engine

--- 

## About

Angeum Engine is a 3D rendering engine built from the ground up with Vulkan API. The engine showcases rendering techniques including GPU-driven rendering, physically-based rendering (PBR), and bindless texture systems. It demonstrates modern Vulkan features such as descriptor indexing, buffer device address, and compute-based culling optimizations.

---

## Screenshots

![PBR Rendering](https://github.com/user-attachments/assets/6c9db11f-4037-49a2-8971-c229a915bd65)
<img width="1482" height="730" alt="cascade_shadows" src="https://github.com/user-attachments/assets/ce88f0b1-7e6a-4a31-85d9-900e4ccfbc88" />

---

## Features

### Rendering Pipeline

1. **Renderer Batching**
   - Ordered by Shader → Material → Mesh for optimal state changes
   - Minimizes pipeline switches and descriptor set rebinds

2. **Vertex Buffer Optimization**
   - Separate vertex buffer per attribute
   - Enables efficient vertex attribute fetching

3. **GLTF Scene Loading**
   - Full glTF 2.0 support with node hierarchy
   - Material, mesh, and texture loading
   - Animation support (planned)

4. **PBR Rendering (Cook-Torrance BRDF)**
   - Normal/Bump mapping
   - Metallic-Roughness workflow
   - Ambient Occlusion mapping
   - Image Based Lighting (IBL)
     - Irradiance map for diffuse
     - Prefiltered environment map for specular
     - BRDF lookup table (LUT)

5. **Image System**
   - KTX texture format support
   - Runtime mipmap generation
   - Cubemap loading and processing

### Memory Management

6. **Dynamic Memory Allocators**
   - Vertex and Index buffers
   - Staging buffers
   - Uniform buffers
   - Device local buffers (Storage, Image, Dedicated memory)

### Multithreading

7. **Asynchronous Processing**
   - Separated queues (Graphics, Compute, Transfer, Present)
   - Secondary command buffer recording for resource loading
   - Round-robin scheduling for load balancing
   - Timeline semaphores for precise frame synchronization

8. **Resource Cache**
   - Prevents duplicate resource creation
   - Shader, pipeline, and descriptor layout caching

### Shader System

9. **Integrated Descriptor Management**
   - Runtime shader compilation using **shaderc**
   - Runtime SPIR-V reflection using **SPIRV-Cross**
   - Automatic descriptor set layout generation
   - Three-tier descriptor set architecture:
     - **Set 0 (Shader)**: Per-shader resources (camera, lights, shadows, IBL)
     - **Set 1 (Material)**: Per-material resources (textures, material properties)
     - **Set 2 (Bindless)**: Global texture arrays (2D, Cubemap) - Optional
   - Per-frame descriptor pool with automatic reset
   - Hash-based shader resource lookup
   - Name-based material resource tracking
   - Lazy descriptor set allocation and updates

### Bindless Rendering

10. **Bindless Texture System**
    - Graceful fallback when hardware doesn't support bindless
    - Up to 4096 textures per type (2D and Cubemap arrays)
    - Generation-based handle validation for safe texture lifetime
    - Automatic detection via SPIR-V reflection
    - Zero-cost abstraction for non-bindless hardware
    - Separate arrays for 2D textures and cubemaps
    - Dynamic texture registration/unregistration
    - Batch descriptor updates for efficiency

### Lighting

11. **Advanced Lighting System**
    - Light types: Directional, Point, Spot
    - Tiled forward rendering with compute-based light culling
    - Per-tile light visibility computation
    - Support for up to 30+ dynamic lights with animated orbits

### GPU-Driven Rendering

12. **GPU-Side Culling and Optimization**
    - Indirect drawing support
    - Unified Mesh Buffer Manager for centralized geometry
    - GPU Frustum Culling via compute shader
    - Two-Pass Occlusion Culling:
      - Pass 1: Render visible objects from previous frame
      - Pass 2: Render newly visible objects
    - Hierarchical depth buffer (Hi-Z) generation
    - Per-mip ImageView for efficient mip chain generation

### Shadow System

13. **Cascaded Shadow Maps (CSM)**
    - Per-cascade light view-projection matrix generation
    - Bounding sphere stabilization to reduce shadow edge shimmer
    - Texture2DArray-based storage for all cascade layers
      
---

## Build Instructions

For detailed build instructions, please see [BUILD.md](BUILD.md).

### Quick Start

**Requirements:**
- Visual Studio 2022
- Vulkan SDK 1.3.275.0 or higher
- CMake 3.20 or higher
- Windows 10/11 (64-bit)

---

## Third-Party Libraries

- **GLFW** 3.3.8 - Window and input handling
- **GLM** - Mathematics library for graphics
- **ImGui** - Immediate mode GUI library
- **KTX** - Khronos texture format support
- **STB** - Image loading (stb_image)
- **TinyGLTF** - glTF 2.0 model loader
- **RapidJSON** - JSON parsing library
- **shaderc** - Runtime GLSL to SPIR-V compilation
- **SPIRV-Cross** - SPIR-V reflection and cross-compilation

---

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

---

## Acknowledgments

- Sponza model by Crytek
- Damaged Helmet model from glTF-Sample-Models
