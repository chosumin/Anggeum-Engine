#pragma once
#include "FrameGraphResource.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class FrameGraph;
	class Texture;
	class Buffer;

	// Handed to each pass's Setup(). Declares the pass's resources and accesses
	// into the owning FrameGraph; the compile phase turns the declarations into
	// lifetimes, barriers and cross-queue sync.
	class FrameGraphBuilder
	{
	public:
		// Transient resources: created (and later aliased) by the graph itself.
		FGTexture CreateTexture(const string& name, const FGTextureDesc& desc);
		FGBuffer CreateBuffer(const string& name, const FGBufferDesc& desc);

		// Migration bridge: wraps a FrameResources/ResourceManager-owned resource.
		// `entryLayout` is the steady-state layout legacy passes leave the image
		// in at frame start (must match the RT desc's initialLayout so the first
		// frame agrees); when UNDEFINED, the FGResourceStateRegistry decides.
		// `exportLayout` is the layout the graph leaves the image in for legacy
		// consumers (VK_IMAGE_LAYOUT_UNDEFINED = don't care).
		// Both layout parameters are bridge-era: once every consumer is migrated
		// the graph sees all accesses, the registry becomes the sole entry-state
		// authority, and this signature collapses to ImportTexture(name, handle).
		// Importing an already-imported name returns the existing virtual handle.
		FGTexture ImportTexture(const string& name, Handle<Texture> texture,
			VkImageLayout entryLayout, VkImageLayout exportLayout);
		FGBuffer ImportBuffer(const string& name, Handle<Buffer> buffer);

		// Blackboard lookup for resources another pass declared earlier.
		FGTexture GetTexture(const string& name) const;
		FGBuffer GetBuffer(const string& name) const;
		bool HasTexture(const string& name) const;

		void Read(FGTexture texture, TextureAccess access);
		void Read(FGBuffer buffer, BufferAccess access);
		FGTexture Write(FGTexture texture, TextureAccess access);
		FGBuffer Write(FGBuffer buffer, BufferAccess access);

		// Declares a write whose barriers the pass records itself (mid-pass
		// transitions the graph cannot see). `finalState` describes the state the
		// pass leaves the resource in; the graph emits no barriers for it but
		// tracks the state for downstream passes.
		FGTexture WriteManual(FGTexture texture, TextureAccess finalState);

		void SetColorAttachment(uint32_t slot, const FGAttachment& decl, uint32_t variant = 0);
		void SetDepthAttachment(const FGAttachment& decl, uint32_t variant = 0);

		// The pass calls context.BeginRendering itself.
		void SetManualRendering();

		// Never cull this pass (it writes state the graph cannot see, e.g. the
		// RendererBatch indirect draw buffers).
		void SetSideEffect();

	private:
		friend class FrameGraph;
		FrameGraphBuilder(FrameGraph& graph, uint32_t passIndex)
			: _graph(graph), _passIndex(passIndex) {}

		void AddAccess(uint32_t resourceIndex, const FGAccessInfo& info);

		FrameGraph& _graph;
		uint32_t _passIndex;
	};
}
