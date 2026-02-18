#pragma once

namespace Core
{
	class Buffer;
	struct MeshAllocation;

	// Same structure as VkDrawIndexedIndirectCommand
	struct DrawIndexedIndirectCommand
	{
		uint32_t indexCount;
		uint32_t instanceCount;
		uint32_t firstIndex;
		int32_t vertexOffset;
		uint32_t firstInstance;
	};

	class IndirectDrawBuffer
	{
	public:
		IndirectDrawBuffer();
		~IndirectDrawBuffer() = default;

		void AddDrawCommand(
			const MeshAllocation& allocation,
			uint32_t materialIndex,
			uint32_t instanceCount,
			uint32_t firstInstance);

		void Clear();

		const vector<DrawIndexedIndirectCommand>& GetDrawCommands() const { return _drawCommands; }
		const vector<uint32_t>& GetMaterialIndices() const { return _materialIndices; }

		uint32_t GetDrawCount() const { return static_cast<uint32_t>(_drawCommands.size()); }

		static constexpr size_t GetDrawCommandSize() { return sizeof(DrawIndexedIndirectCommand); }

	private:
		vector<DrawIndexedIndirectCommand> _drawCommands;
		vector<uint32_t> _materialIndices;
	};
}