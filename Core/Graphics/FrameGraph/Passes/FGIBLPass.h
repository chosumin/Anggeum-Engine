#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class Device;
	class Scene;
	class Texture;
	class PreEnvironmentPass;
	class BrdfLutPass;

	// Builds the image-based-lighting inputs (irradiance + prefiltered cubemaps,
	// BRDF LUT) from the scene's environment map.
	class FGIBLPass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_OFFSCREEN = "Offscreen";
		static constexpr const char* RT_IRRADIANCE = "Irradiance";
		static constexpr const char* RT_PREFILTERED = "Prefiltered";
		static constexpr const char* RT_BRDF_LUT = "BrdfLut";

		// Bindless indices of the three maps above, consumed by the geometry pass.
		// Written every frame (the value is CPU-side state here, and each frame
		// slot owns its own copy of the buffer).
		static constexpr const char* UB_GI = "IBL.GI";

		FGIBLPass(Device& device, Scene& scene);
		~FGIBLPass();

		const char* GetName() const override { return "FGIBLPass"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		// Creates the render targets, imports them (WriteManual — the generators
		// manage layouts themselves), builds the generators and registers the
		// cubemaps/LUT to bindless. Runs on the first frame only.
		void CreateResources(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame);
		void RegisterGiTexturesToBindless(RenderFrame& renderFrame,
			Handle<Texture> irradiance, Handle<Texture> prefiltered, Handle<Texture> brdfLut);

		Device& _device;
		Scene& _scene;

		unique_ptr<PreEnvironmentPass> _preEnvironmentPass;
		unique_ptr<BrdfLutPass> _brdfLutPass;

		// Bindless indices, filled once and re-uploaded every frame so both frame
		// slots carry them.
		GI _giBuffer;

		bool _generated = false;
		bool _record = false;

		// Valid only on the frame the generation runs; that is also the only frame
		// the pass declares them.
		FGTexture _offscreen;
		FGTexture _irradiance;
		FGTexture _prefiltered;
		FGTexture _brdfLut;
	};
}
