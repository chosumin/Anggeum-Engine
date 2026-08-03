#include "stdafx.h"
#include "FrameGraphBuilder.h"
#include "FrameGraph.h"

namespace Core
{
	FGTexture FrameGraphBuilder::CreateTexture(const string& name, const FGTextureDesc& desc)
	{
		auto it = _graph._resourceIndices.find(name);
		if (it != _graph._resourceIndices.end())
		{
			assert(_graph._resources[it->second].isTexture && !_graph._resources[it->second].imported &&
				"name already declared with a different resource kind");
			return FGTexture{ it->second };
		}

		FGResourceDecl decl;
		decl.name = name;
		decl.isTexture = true;
		decl.imported = false;
		decl.texDesc = desc;

		return FGTexture{ _graph.DeclareResource(std::move(decl)) };
	}

	FGBuffer FrameGraphBuilder::CreateBuffer(const string& name, const FGBufferDesc& desc)
	{
		auto it = _graph._resourceIndices.find(name);
		if (it != _graph._resourceIndices.end())
		{
			assert(!_graph._resources[it->second].isTexture && !_graph._resources[it->second].imported &&
				"name already declared with a different resource kind");
			return FGBuffer{ it->second };
		}

		FGResourceDecl decl;
		decl.name = name;
		decl.isTexture = false;
		decl.imported = false;
		decl.bufDesc = desc;

		return FGBuffer{ _graph.DeclareResource(std::move(decl)) };
	}

	FGTexture FrameGraphBuilder::ImportTexture(const string& name, Handle<Texture> texture)
	{
		auto it = _graph._resourceIndices.find(name);
		if (it != _graph._resourceIndices.end())
		{
			auto& existing = _graph._resources[it->second];
			assert(existing.isTexture && existing.imported && existing.importedTexture == texture &&
				"name already imported with a different physical resource");
			return FGTexture{ it->second };
		}

		FGResourceDecl decl;
		decl.name = name;
		decl.isTexture = true;
		decl.imported = true;
		decl.importedTexture = texture;

		return FGTexture{ _graph.DeclareResource(std::move(decl)) };
	}

	FGBuffer FrameGraphBuilder::ImportBuffer(const string& name, Handle<Buffer> buffer)
	{
		auto it = _graph._resourceIndices.find(name);
		if (it != _graph._resourceIndices.end())
		{
			auto& existing = _graph._resources[it->second];
			assert(!existing.isTexture && existing.imported && existing.importedBuffer == buffer &&
				"name already imported with a different physical resource");
			return FGBuffer{ it->second };
		}

		FGResourceDecl decl;
		decl.name = name;
		decl.isTexture = false;
		decl.imported = true;
		decl.importedBuffer = buffer;

		return FGBuffer{ _graph.DeclareResource(std::move(decl)) };
	}

	FGTexture FrameGraphBuilder::GetTexture(const string& name) const
	{
		auto it = _graph._resourceIndices.find(name);
		assert(it != _graph._resourceIndices.end() && "unknown frame graph texture name");
		assert(_graph._resources[it->second].isTexture);
		return FGTexture{ it->second };
	}

	FGBuffer FrameGraphBuilder::GetBuffer(const string& name) const
	{
		auto it = _graph._resourceIndices.find(name);
		assert(it != _graph._resourceIndices.end() && "unknown frame graph buffer name");
		assert(!_graph._resources[it->second].isTexture);
		return FGBuffer{ it->second };
	}

	bool FrameGraphBuilder::HasTexture(const string& name) const
	{
		return _graph._resourceIndices.count(name) != 0;
	}

	bool FrameGraphBuilder::HasBuffer(const string& name) const
	{
		return _graph._resourceIndices.count(name) != 0;
	}

	void FrameGraphBuilder::AddAccess(uint32_t resourceIndex, const FGAccessInfo& info)
	{
		auto& decl = _graph._passDecls[_passIndex];

		// A pass may declare the same (resource, access) pair more than once (e.g.
		// both rendering variants attach the same image) — record it only once.
		for (auto& access : decl.accesses)
		{
			if (access.resource == resourceIndex &&
				access.info.stage == info.stage &&
				access.info.access == info.access &&
				access.info.layout == info.layout &&
				access.info.isWrite == info.isWrite)
				return;
		}

		FGAccessDecl access;
		access.resource = resourceIndex;
		access.info = info;
		decl.accesses.push_back(access);
	}

	void FrameGraphBuilder::Read(FGTexture texture, TextureAccess access)
	{
		assert(texture.IsValid());
		AddAccess(texture.index, GetAccessInfo(access));
	}

	void FrameGraphBuilder::Read(FGBuffer buffer, BufferAccess access)
	{
		assert(buffer.IsValid());
		AddAccess(buffer.index, GetAccessInfo(access));
	}

	FGTexture FrameGraphBuilder::Write(FGTexture texture, TextureAccess access)
	{
		assert(texture.IsValid());
		FGAccessInfo info = GetAccessInfo(access);
		assert(info.isWrite && "Write() called with a read-only access");
		AddAccess(texture.index, info);
		return texture;
	}

	FGBuffer FrameGraphBuilder::Write(FGBuffer buffer, BufferAccess access)
	{
		assert(buffer.IsValid());
		FGAccessInfo info = GetAccessInfo(access);
		assert(info.isWrite && "Write() called with a read-only access");
		AddAccess(buffer.index, info);
		return buffer;
	}

	FGTexture FrameGraphBuilder::WriteManual(FGTexture texture, TextureAccess finalState)
	{
		assert(texture.IsValid());
		FGAccessInfo info = GetAccessInfo(finalState);
		info.isWrite = true;
		info.manualBarriers = true;
		AddAccess(texture.index, info);
		return texture;
	}

	void FrameGraphBuilder::SetColorAttachment(uint32_t slot, const FGAttachment& decl, uint32_t variant)
	{
		assert(variant < FrameGraphPassContext::MaxRenderingVariants);
		assert(decl.texture.IsValid());

		auto& pass = _graph._passRecords[_passIndex];
		auto& colors = pass.colorAttachments[variant];
		if (colors.size() <= slot)
			colors.resize(slot + 1);
		colors[slot] = decl;
		pass.hasRendering[variant] = true;

		Write(decl.texture, decl.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD ?
			TextureAccess::ColorLoadWrite : TextureAccess::ColorWrite);

		if (decl.resolveTarget.IsValid())
			Write(decl.resolveTarget, TextureAccess::ColorWrite);
	}

	void FrameGraphBuilder::SetDepthAttachment(const FGAttachment& decl, uint32_t variant)
	{
		assert(variant < FrameGraphPassContext::MaxRenderingVariants);
		assert(decl.texture.IsValid());

		auto& pass = _graph._passRecords[_passIndex];
		pass.depthAttachments[variant] = decl;
		pass.hasDepth[variant] = true;
		pass.hasRendering[variant] = true;

		Write(decl.texture, decl.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD ?
			TextureAccess::DepthLoadWrite : TextureAccess::DepthWrite);
	}


	void FrameGraphBuilder::SetSideEffect()
	{
		_graph._passDecls[_passIndex].sideEffect = true;
	}
}
