![pbrcapture](https://github.com/user-attachments/assets/6c9db11f-4037-49a2-8971-c229a915bd65)

Graphics API
- Vulkan
  
Features
1. Instanced rendering using Push Constants.
2. Multithreaded render pass.
3. Renderer batching (Ordered by Shader > Material > Mesh).
4. Separate vertex buffer per attribute for vertex attribute optimization.
5. GLTF scene loading.
6. PBR rendering (Cook-Torrance BRDF)
	- Bump, Metallic, Roughness and Occlusion mapping
	- Image Based Rendering (Irradiance, Prefiltered, BRDF LUT)
7. Image
	- Supports KTX extension
	- Supports Mipmap generation
8. Dynamic memory allocator for
	- Vertex and Index buffers
	- Staging buffers
	- Uniform buffers
	- Image buffers (also support dedicated memory)
9. Multithreading
	- Separated queues (Graphics, Compute, Tranfer, Present)
	
Third Parties
- imgui
- stb
- tinygltf
- rapidjson
- glm
- glfw
- ktx
