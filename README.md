![pbrcapture](https://github.com/user-attachments/assets/6c9db11f-4037-49a2-8971-c229a915bd65)

Graphics API
- Vulkan
  
Features
1. Instanced rendering using Push Constants.
2. Renderer batching (Ordered by Shader > Material > Mesh).
3. Separate vertex buffer per attribute for vertex attribute optimization.
4. GLTF scene loading.
5. PBR rendering (Cook-Torrance BRDF)
	- Bump, Metallic, Roughness and Occlusion mapping
	- Image Based Rendering (Irradiance, Prefiltered, BRDF LUT)
6. Image
	- Supports KTX extension
	- Supports Mipmap generation
7. Dynamic memory allocator for
	- Vertex and Index buffers
	- Staging buffers
	- Uniform buffers
	- Image buffers (also support dedicated memory)
8. Multithreading
	- Separated queues (Graphics, Compute, Tranfer, Present)
	- Loading images and buffers by using secondary command buffers and a single primary buffer
	- Round-robin scheduling
9. Resource cache
10. Shader system
	- Runtime shader compile using shaderc
	- Runtime shader reflection using SPIRV_Cross
11. Lighting
	- Directional, Point, Spot

Third Parties
- imgui
- stb
- tinygltf
- rapidjson
- glm
- glfw
- ktx
