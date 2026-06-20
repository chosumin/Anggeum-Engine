# Angeum Engine

--- 

## About

Toy 3D rendering engine for studying Vulkan API.

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
   - Separate vertex buffer per attribute (SOA)

3. **GLTF Scene Loading**
   - Full glTF 2.0 support with node hierarchy
   - Material, mesh, and texture loading

4. **PBR Rendering (Cook-Torrance BRDF)**
   - Normal / Metallic-Roughness / AO
   - Image Based Lighting (IBL)
     - Irradiance map for diffuse
     - Prefiltered environment map for specular
     - BRDF lookup table (LUT)

5. **Image System**
   - KTX texture format support
   - Runtime mipmap generation
   - Cubemap loading and processing

### Memory Management

6. **Custom Memory Allocators**
   - Vertex and Index buffers
   - Staging / Uniform buffers
   - Device local buffers (Storage, Image, Dedicated memory)

### Multithreading

7. **Asynchronous Processing**
   - Separated queues (Graphics, Compute, Transfer, Present)
   - Secondary command buffer recording for resource loading
   - Timeline semaphores for precise frame synchronization

### Shader System

9. **Integrated Descriptor Management**
   - Runtime shader compilation using **shaderc**
   - Runtime SPIR-V reflection using **SPIRV-Cross**
   - Automatic descriptor set layout generation
   - Per-frame descriptor pool with automatic reset

### Bindless Rendering

10. **Bindless Texture System**
    - Graceful fallback when hardware doesn't support bindless
    - Up to 4096 textures per type (2D and Cubemap arrays)
    - Generation-based handle validation for safe texture lifetime
    - Automatic detection via SPIR-V reflection

### Lighting

11. **Lighting System**
    - Light types: Directional, Point, Spot
    - Forward+ rendering for light culling
    - Per-tile light visibility computation

### GPU-Driven Rendering

12. **GPU-Side Culling and Optimization**
    - Multi Draw Indirect support
    - GPU Frustum Culling via compute shader
    - Two-Pass Occlusion Culling

### Shadow System

13. **Cascaded Shadow Maps (CSM) with PCSS**
    - Practical split scheme (logarithmic + uniform hybrid) for cascade partitioning
    - Blocker search with Poisson disk sampling
    - Interleaved Gradient Noise for per-fragment sample rotation
    - Filter radius clamping to prevent extreme sampling artifacts

14. **Distance Field Shadows**
    - Render shadows over large distances using Global SDF Volumes
    - Use Inigo Quilez's improved soft shadow with a parabolic closest-approach estimate

15. **Distance Field Ambient Occlusion**
    - SDF-based screen-space ambient occlusion using hemisphere sampling

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

## Acknowledgments

- Sponza model by Crytek
- Damaged Helmet model from glTF-Sample-Models
