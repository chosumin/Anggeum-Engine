#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class ResourceManager;
	class Texture;
	class HiZPyramid;

	class HiZGeneratePass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_HIZ_FINAL = "OcclusionCull.HiZFinal";

		HiZGeneratePass(ResourceManager& resourceManager, VkExtent2D screenExtent);
		~HiZGeneratePass() override;

		const char* GetName() const override { return "HiZGenerate"; }
		QueueType GetQueueType() const override { return QueueType::Compute; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		unique_ptr<HiZPyramid> _pyramid;

		bool _active = false;
		FGTexture _depth;
		Handle<Texture> _hiZTexture;
	};
}
