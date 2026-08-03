# Angeum Engine

--- 

## About

Toy 3D rendering engine for learning modern rendering and Vulkan API.

---

## Screenshots

![PBR Rendering](https://github.com/user-attachments/assets/6c9db11f-4037-49a2-8971-c229a915bd65)
<img width="1913" height="1077" alt="capture2" src="https://github.com/user-attachments/assets/5379fa9e-7022-4219-983c-737eec95580c" />

---

## Features

### [Frame Graph](Core/Graphics/FrameGraph/README.md)

**Declarative Render Passes**
- Passes declare resource reads/writes; barriers and pass culling are derived automatically (Vulkan 1.4 dynamic rendering)

**Async Compute**
- Dedicated compute queue alongside graphics, with cross-queue dependencies expressed via timeline semaphores

**Transient Resource Aliasing**
- Graph-owned transients with disjoint lifetimes alias the same memory via greedy interval placement

**Multithreaded Recording**
- One command buffer per pass, recorded in parallel by a worker thread pool off the main thread

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

### Resource Management

**Custom Memory Allocators**
   - Vertex and Index buffers
   - Staging / Uniform buffers
   - Device local buffers (Storage, Image, Dedicated memory)

**Generational Handle System**
   - Pool-owned resources referenced via `Handle<T>` (index + generation)
   - Stale handles resolve to null instead of aliasing recycled slots
   - In-place buffer replacement keeps held handles valid across resizes/rebuilds

**Asynchronous Uploads**
   - Buffer and image uploads staged on worker threads and submitted as a single batch through a transfer context

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
