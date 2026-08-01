#include "stdafx.h"
#include "FrameGraph.h"
#include "TransientResourceAllocator.h"

// Synthetic self-tests for the device-independent halves of the frame graph:
// the compile core (culling, lifetimes, barriers, cross-queue sync points) and
// the transient placement algorithm. Free functions — they only exercise the
// public compile/placement APIs, so the classes carry no test hooks. Debug
// builds call RunFrameGraphSelfTests once at first FrameGraph construction
// (forward-declared in FrameGraph.cpp).

namespace Core
{
	namespace
	{
		void RunPlacementSelfTests()
		{
			using Allocator = TransientResourceAllocator;

			// Two same-size resources with disjoint lifetimes alias the same offset;
			// an overlapping-lifetime third lands after them.
			{
				vector<Allocator::PlaceInput> inputs = {
					{ 100, 1, 0, 1 },  // A
					{ 100, 1, 2, 3 },  // B: disjoint from A -> aliases offset 0
					{ 100, 1, 1, 2 },  // C: overlaps both -> its own range
				};
				vector<Allocator::Placement> placements;
				VkDeviceSize heapSize = Allocator::Place(inputs, placements);

				assert(placements[0].offset == placements[1].offset);
				assert(placements[2].offset != placements[0].offset);
				assert(heapSize == 200);
			}

			// Alignment is honored and sizes drive placement order (largest first).
			{
				vector<Allocator::PlaceInput> inputs = {
					{ 64, 256, 0, 0 },
					{ 512, 256, 0, 0 },
					{ 128, 256, 0, 0 },
				};
				vector<Allocator::Placement> placements;
				VkDeviceSize heapSize = Allocator::Place(inputs, placements);

				for (const auto& p : placements)
					assert(p.offset % 256 == 0);

				// All lifetimes overlap: no aliasing, all ranges disjoint.
				for (size_t i = 0; i < placements.size(); i++)
					for (size_t j = i + 1; j < placements.size(); j++)
					{
						const bool overlap =
							placements[i].offset < placements[j].offset + placements[j].size &&
							placements[j].offset < placements[i].offset + placements[i].size;
						assert(!overlap);
					}
				assert(heapSize >= 512 + 128 + 64);
			}

			// Chain of disjoint lifetimes collapses into one slot.
			{
				vector<Allocator::PlaceInput> inputs = {
					{ 300, 1, 0, 0 },
					{ 200, 1, 1, 1 },
					{ 100, 1, 2, 2 },
				};
				vector<Allocator::Placement> placements;
				VkDeviceSize heapSize = Allocator::Place(inputs, placements);
				assert(heapSize == 300);
				assert(placements[0].offset == 0 && placements[1].offset == 0 && placements[2].offset == 0);
			}

			// Disjoint lifetimes on different queues must NOT share memory: pass
			// indices do not order execution across queues, so the "earlier"
			// resource may still be in flight when the "later" one writes.
			{
				vector<Allocator::PlaceInput> inputs = {
					{ 100, 1, 0, 1, 0 },   // graphics
					{ 100, 1, 2, 3, 1 },   // compute, lifetime disjoint
				};
				vector<Allocator::Placement> placements;
				VkDeviceSize heapSize = Allocator::Place(inputs, placements);
				assert(placements[0].offset != placements[1].offset);
				assert(heapSize == 200);
			}

			printf("[TransientResourceAllocator] placement self-tests passed\n");
		}
	}

	void RunFrameGraphSelfTests()
	{
		// Resources: 0 = transient tex A, 1 = transient tex B (never read),
		// 2 = imported tex C, 3 = transient buffer D.
		vector<FGResourceDecl> resources(4);
		resources[0].name = "A"; resources[0].isTexture = true;
		resources[1].name = "B"; resources[1].isTexture = true;
		resources[2].name = "C"; resources[2].isTexture = true;
		resources[2].imported = true;
		resources[2].entryLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		resources[2].exportLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		resources[3].name = "D"; resources[3].isTexture = false;

		auto makeAccess = [](uint32_t res, TextureAccess access) {
			FGAccessDecl decl; decl.resource = res; decl.info = GetAccessInfo(access); return decl;
		};
		auto makeBufferAccess = [](uint32_t res, BufferAccess access) {
			FGAccessDecl decl; decl.resource = res; decl.info = GetAccessInfo(access); return decl;
		};

		// P0 (graphics): writes A, writes B.
		// P1 (compute):  reads A, writes D, writes C (imported).
		// P2 (graphics): reads D.  (side effect, e.g. draws to external state)
		// P3 (graphics): writes B only -> culled (nothing reads B).
		vector<FGPassDecl> passes(4);
		passes[0].queue = QueueType::Graphics;
		passes[0].accesses = { makeAccess(0, TextureAccess::ColorWrite),
							   makeAccess(1, TextureAccess::ColorWrite) };
		passes[1].queue = QueueType::Compute;
		passes[1].accesses = { makeAccess(0, TextureAccess::SampledCompute),
							   makeBufferAccess(3, BufferAccess::StorageComputeWrite),
							   makeAccess(2, TextureAccess::StorageComputeWrite) };
		passes[2].queue = QueueType::Graphics;
		passes[2].sideEffect = true;
		passes[2].accesses = { makeBufferAccess(3, BufferAccess::StorageFragmentRead) };
		passes[3].queue = QueueType::Graphics;
		passes[3].accesses = { makeAccess(1, TextureAccess::ColorWrite) };

		vector<FGResourceState> entryStates(4);
		entryStates[2] = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT };

		FGCompileOutput out;
		FrameGraph::CompileDeclarations(passes, resources, entryStates, out);

		// Culling: P3 culled (B unread); everything else alive.
		assert(!out.culledPasses[0] && !out.culledPasses[1] && !out.culledPasses[2] && out.culledPasses[3]);

		// Cross-queue sync: P1 (compute) waits on P0 (graphics, produced A);
		// P2 (graphics) waits on P1 (compute, produced D). Producers signal.
		assert(out.passSync[1].waitPass == 0);
		assert(out.passSync[2].waitPass == 1);
		assert(out.passSync[0].signals && out.passSync[1].signals);
		assert(!out.passSync[2].signals);

		// Lifetimes: A spans P0..P1 and is cross-queue; D spans P1..P2.
		assert(out.lifetimes[0].firstPass == 0 && out.lifetimes[0].lastPass == 1);
		assert(out.lifetimes[0].crossQueue);
		assert(out.lifetimes[3].firstPass == 1 && out.lifetimes[3].lastPass == 2);
		assert(out.lifetimes[1].used); // written by P0 (alive) even though unread

		// Barriers: P0's write to A transitions UNDEFINED -> COLOR_ATTACHMENT.
		{
			bool found = false;
			for (const auto& plan : out.preBarriers[0])
				if (plan.resource == 0 &&
					plan.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
					plan.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
					found = true;
			assert(found);
		}

		// P1's read of A transitions COLOR_ATTACHMENT -> SHADER_READ_ONLY with a
		// cross-queue source (NONE src stage: the timeline wait orders execution).
		{
			bool found = false;
			for (const auto& plan : out.preBarriers[1])
				if (plan.resource == 0 &&
					plan.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
					plan.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
					plan.srcStage == VK_PIPELINE_STAGE_2_NONE)
					found = true;
			assert(found);
		}

		// Imported C: written as GENERAL by P1, exported back to SHADER_READ_ONLY
		// via a post-barrier on its last accessing pass (P1).
		{
			bool found = false;
			for (const auto& plan : out.postBarriers[1])
				if (plan.resource == 2 &&
					plan.oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
					plan.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
					found = true;
			assert(found);
			assert(out.finalStates[2].layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		// Re-running culling with no readers of D and no side effect.
		{
			vector<FGPassDecl> passes2 = { passes[0], passes[1] };
			passes2[1].accesses = { makeBufferAccess(3, BufferAccess::StorageComputeWrite) };
			vector<FGResourceDecl> resources2 = resources;
			resources2[2].imported = false; // C no longer keeps P1 alive

			FGCompileOutput out2;
			FrameGraph::CompileDeclarations(passes2, resources2, entryStates, out2);

			// P1 writes only unread D -> culled; that drops P1's read of A, so
			// P0's outputs are both unread -> P0 culled transitively.
			assert(out2.culledPasses[1]);
			assert(out2.culledPasses[0]);
		}

		printf("[FrameGraph] self-tests passed\n");

		RunPlacementSelfTests();
	}
}
