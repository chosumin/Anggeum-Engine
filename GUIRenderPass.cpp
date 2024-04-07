#include "stdafx.h"
#include "GUIRenderPass.h"
#include "SwapChain.h"
#include "Framebuffer.h"
#include "CommandBuffer.h"

GUIRenderPass::GUIRenderPass(Core::Device& device, Core::SwapChain& swapChain,
	RenderTarget* colorRenderTarget)
	:RenderPass(device)
{
	auto extent = swapChain.GetSwapChainExtent();
	CreateColorAttachment(colorRenderTarget,
		VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
	CreateRenderPass();

	_framebuffer = new Framebuffer(device, swapChain, *this);
}

GUIRenderPass::~GUIRenderPass()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	vkDestroyDescriptorPool(_device.GetDevice(), _pool, nullptr);
}

void GUIRenderPass::Prepare()
{
	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = static_cast<uint32_t>(size(pool_sizes));
	pool_info.pPoolSizes = pool_sizes;

	vkCreateDescriptorPool(_device.GetDevice(), &pool_info, nullptr, &_pool);

	//this initializes the core structures of imgui
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	ImGui_ImplGlfw_InitForVulkan(Window::Instance().GetWindow(), true);

	//this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _device.GetInstance();
	init_info.PhysicalDevice = _device.GetPhysicalDevice();
	init_info.Device = _device.GetDevice();
	init_info.Queue = _device.GetGraphicsQueue();
	init_info.DescriptorPool = _pool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_8_BIT;
	init_info.RenderPass = GetHandle();
	ImGui_ImplVulkan_Init(&init_info);
}

void GUIRenderPass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	auto framebuffer = _framebuffer->GetHandle(imageIndex);
	auto renderPassBeginInfo = CreateRenderPassBeginInfo(framebuffer, _framebuffer->GetExtent());
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
		commandBuffer.GetHandle());

	commandBuffer.EndRenderPass();
}

void GUIRenderPass::Update()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	//ImGui::ShowDemoWindow();

	UpdateFrame();
}

void GUIRenderPass::UpdateFrame()
{
	static float f = 0.0f;
	static int counter = 0;

	ImGui::Begin("Hello, world!");

	ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	ImGui::End();
}
