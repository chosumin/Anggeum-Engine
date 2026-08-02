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

		// Wraps a resource owned by different resource managers.
		FGTexture ImportTexture(const string& name, Handle<Texture> texture);
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
