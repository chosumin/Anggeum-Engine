# Angeum Engine

--- 

## About

Toy 3D rendering engine for studying Vulkan API.

---

## Screenshots

![PBR Rendering](https://github.com/user-attachments/assets/6c9db11f-4037-49a2-8971-c229a915bd65)
<img width="1902" height="1068" alt="capture" src="https://github.com/user-attachments/assets/7ed46b76-373d-4335-8542-51f0a0acdbdd" />

---

## Features

### Rendering Pipeline

**Renderer Batching**
   - Ordered by Shader → Material → Mesh for optimal state changes

**Vertex Buffer Optimization**
   - Separate vertex buffer per attribute (SOA)

**GLTF Scene Loading**
   - Full glTF 2.0 support with node hierarchy
   - Material, mesh, and texture loading

**PBR Rendering (Cook-Torrance BRDF)**
   - Normal / Metallic-Roughness / AO
   - Image Based Lighting (IBL)
     - Irradiance map for diffuse
     - Prefiltered environment map for specular
     - BRDF lookup table (LUT)

**Image System**
   - KTX texture format support
   - Runtime mipmap generation
   - Cubemap loading and processing

### Memory Management

**Custom Memory Allocators**
   - Vertex and Index buffers
   - Staging / Uniform buffers
   - Device local buffers (Storage, Image, Dedicated memory)

### Multithreading

**Asynchronous Processing**
   - Separated queues (Graphics, Compute, Transfer, Present)
   - Secondary command buffer recording for resource loading
   - Timeline semaphores for precise frame synchronization

### Shader System

**Integrated Descriptor Management**
   - Runtime shader compilation using **shaderc**
   - Runtime SPIR-V reflection using **SPIRV-Cross**
   - Automatic descriptor set layout generation
   - Per-frame descriptor pool with automatic reset

### Lighting

**Lighting System**
- Light types: Directional, Point, Spot
- Forward+ rendering for light culling with per-tile light visibility computation

**Cascaded Shadow Maps with PCSS**
- Practical split scheme (logarithmic + uniform hybrid) for cascade partitioning
- Blocker search with Poisson disk sampling and Interleaved Gradient Noise for sample rotation
- Filter radius clamping to prevent extreme sampling artifacts

**Ambient Occlusion**
- Runtime toggle between DFAO and CACAO
    - **Distance Field Ambient Occlusion**
    - **FFX CACAO (Combined Adaptive Compute Ambient Occlusion)**



### GPU-Driven Rendering

**GPU-Side Culling and Optimization**
- Multi Draw Indirect support
- GPU Frustum Culling via compute shader
- Two-Pass Occlusion Culling

**Bindless Texture System**
- Graceful fallback when hardware doesn't support bindless
- Up to 4096 textures per type (2D and Cubemap arrays)
- Generation-based handle validation for safe texture lifetime
- Automatic detection via SPIR-V reflection

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
- **AMD FidelityFX CACAO** - Combined Adaptive Compute Ambient Occlusion