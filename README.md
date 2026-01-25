![pbrcapture](https://github.com/user-attachments/assets/6c9db11f-4037-49a2-8971-c229a915bd65)

Graphics API
- Vulkan
  
Features
1. Renderer batching
	- Ordered by Shader > Material > Mesh
	- Instanced rendering by using SSBO
2. Separate vertex buffer per attribute for vertex attribute optimization.
3. GLTF scene loading.
4. PBR rendering (Cook-Torrance BRDF)
	- Bump, Metallic, Roughness and Occlusion mapping
	- Image Based Rendering (Irradiance, Prefiltered, BRDF LUT)
5. Image
	- Supports KTX extension
	- Supports Mipmap generation
6. Dynamic memory allocator for
	- Vertex and Index buffers
	- Staging buffers
	- Uniform buffers
	- Image buffers (also support dedicated memory)
7. Multithreading
	- Separated queues (Graphics, Compute, Transfer, Present)
	- Loading images and buffers by using secondary command buffers and a single primary buffer
	- Round-robin scheduling
	- Timeline semaphores for frame synchronization
8. Resource cache
9. Shader system with integrated descriptor management
	- Runtime shader compile using shaderc
	- Runtime shader reflection using SPIRV_Cross
	- Automatic descriptor set layout generation from shader reflection
	- Three-tier descriptor set architecture:
		- Set 0 (Shader): Shared per-shader resources (camera, lights, shadows, IBL)
		- Set 1 (Material): Per-material resources (textures, material properties)
		- Set 2 (Bindless): Global texture arrays (2D textures, cubemaps) - Optional
	- Per-frame descriptor pool with automatic reset
	- Hash-based shader resource lookup for efficient binding
	- Name-based material resource tracking
	- Lazy descriptor set allocation and update on first bind
10. Bindless Texture System
	- Graceful fallback to traditional descriptor bindings
	- Support for up to 4096 textures per type (2D and Cubemap)
	- Generation-based handle validation for safe texture lifetime management
	- Automatic shader detection via SPIRV reflection
	- Zero-cost abstraction when hardware doesn't support bindless
	- Separate arrays for 2D textures and cubemaps
	- Dynamic texture registration/unregistration
	- Batch descriptor updates for efficiency
11. Lighting
	- Directional, Point, Spot
	- Tiled forward rendering with light culling compute shader

Third Parties
- imgui
- stb
- tinygltf
- rapidjson
- glm
- glfw
- ktx
- shaderc
- SPIRV-Cross
