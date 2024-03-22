#pragma once

namespace Core
{
	class RenderPass;
	class ImGuiManager
	{
	public:
		ImGuiManager(Device& device, RenderPass& renderPass);
		~ImGuiManager();

		void Update();
	private:
		void UpdateFrame();
	private:
		Device& _device;
		VkDescriptorPool _pool;
	};
}

