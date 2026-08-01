#pragma once
#include "FrameGraphResource.h"
#include "FrameGraphPass.h"
#include "ResourceStateRegistry.h"
#include "Graphics/ResourceHandle.h"
#include "Foundation/Threadable.h"

namespace Core
{
	class Device;
	class RenderContext;
	class RenderFrame;
	class Texture;
	class Buffer;
	class Sampler;
	class WorkerThreadManager;
	class GpuQueueTimer;
	class CommandBuffer;
	class FrameGraphRecordJob;
	class TransientResourceAllocator;

	// Declaration of one virtual resource for the current frame. Rebuilt every
	// frame by the Setup sweep.
	struct FGResourceDecl
	{
		string name;
		bool isTexture = true;
		bool imported = false;

		// Transient descriptions (graph-created).
		FGTextureDesc texDesc{};
		FGBufferDesc bufDesc{};

		// Imported physical resources (FrameResources/ResourceManager-owned).
		Handle<Texture> importedTexture;
		Handle<Buffer> importedBuffer;

		// Imported textures only: layout assumed on the first-ever frame, and the
		// layout to leave behind for legacy consumers (UNDEFINED = don't care).
		VkImageLayout entryLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout exportLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	struct FGAccessDecl
	{
		uint32_t resource = UINT32_MAX;
		FGAccessInfo info{};
	};

	// Declaration of one pass for the current frame.
	struct FGPassDecl
	{
		FrameGraphPass* pass = nullptr;
		QueueType queue = QueueType::Graphics;
		bool sideEffect = false;
		bool manualRendering = false;

		vector<FGAccessDecl> accesses; // declaration order

		array<vector<FGAttachment>, FrameGraphPassContext::MaxRenderingVariants> colorAttachments;
		array<FGAttachment, FrameGraphPassContext::MaxRenderingVariants> depthAttachments;
		array<bool, FrameGraphPassContext::MaxRenderingVariants> hasDepth{};
		array<bool, FrameGraphPassContext::MaxRenderingVariants> hasRendering{};
	};

	// One planned barrier: symbolic (resource index) at compile time, turned into
	// a VkImage/BufferMemoryBarrier2 against the realized resource at record time.
	struct FGBarrierPlan
	{
		uint32_t resource = UINT32_MAX;
		bool isImage = true;
		VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
		VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 dstAccess = VK_ACCESS_2_NONE;
		VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // images only
		VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED; // images only
	};

	// Cross-queue sync points for one alive pass, derived at compile time from
	// the resource dependencies.
	struct FGPassSync
	{
		// Latest cross-queue producer pass this pass must wait for (-1 = none).
		// With two queues every cross-queue producer sits on the opposite queue.
		// Becomes a per-queue slot again if a third queue ever joins the graph.
		int32_t waitPass = -1;
		VkPipelineStageFlags waitStages = 0;

		// True when a later pass on another queue depends on this one.
		bool signals = false;

		// Timeline value this pass signals; assigned while building SubmitInfos.
		u64 signalValue = 0;
	};

	struct FGLifetime
	{
		uint32_t firstPass = UINT32_MAX;
		uint32_t lastPass = 0;
		bool used = false;

		// True when the resource is read from a queue other than the one that
		// wrote it. Conservatively excludes it from memory aliasing (pass indices
		// do not order execution across queues).
		bool crossQueue = false;
	};

	struct FGCompileOutput
	{
		vector<uint8_t> culledPasses;  // per pass
		vector<FGLifetime> lifetimes;  // per resource
		vector<FGPassSync> passSync;   // per pass (alive passes only)
		vector<vector<FGBarrierPlan>> preBarriers;  // per pass
		vector<vector<FGBarrierPlan>> postBarriers; // per pass
		vector<FGResourceState> finalStates;        // per resource (imported: fed back to registry)
	};

	class FrameGraph : public Threadable
	{
	public:
		FrameGraph(Device& device, WorkerThreadManager& workerThreadManager);
		~FrameGraph();

		FrameGraph(const FrameGraph&) = delete;
		FrameGraph& operator=(const FrameGraph&) = delete;

		void AddPass(unique_ptr<FrameGraphPass> pass);
		bool HasPasses() const { return !_passes.empty(); }

		void OnGUI(RenderFrame& renderFrame);

		// Setup sweep + compile + transient realization +
		// physical resolution + per-pass context build.
		void SetupAndCompile(RenderFrame& renderFrame, uint32_t imageIndex);

		// Records every alive pass. 
		// During the recording window the main thread only waits, 
		// so the resource pools are frozen and worker-side Handle<T>::Get() is race-free. 
		uint32_t Execute(RenderContext& renderContext, RenderFrame& renderFrame);

		// Drop realized transients and compiled state (swapchain resize etc.).
		void Invalidate();

		// Human-readable dump of the last compile (pass order, culling, batches,
		// barriers) for debugging.
		string DumpCompiled() const;

		void OnDebugGUI() const;

		// Device-independent compile core. Public and static so the synthetic
		// self-tests (FrameGraphSelfTests.cpp) can exercise it directly.
		static void CompileDeclarations(const vector<FGPassDecl>& passes,
			const vector<FGResourceDecl>& resources,
			const vector<FGResourceState>& entryStates,
			FGCompileOutput& out);

	private:
		friend class FrameGraphBuilder;
		friend class FrameGraphRecordJob;

		void RecordPass(CommandBuffer& commandBuffer, uint32_t passIndex, uint32_t timerPassIndex);

		// Builder backend.
		uint32_t DeclareResource(FGResourceDecl&& decl);
		FGResourceDecl& GetResourceDecl(uint32_t index) { return _resources[index]; }

		// Capture the state of imported resources.
		void CollectEntryStates(vector<FGResourceState>& outEntryStates);
		void RealizeTransients(RenderFrame& renderFrame);
		void ResolvePhysical();
		void BuildPassContexts(RenderFrame& renderFrame, uint32_t imageIndex);

		void UpdateRegistry();

		static FGAccessInfo GetExportAccessInfo(VkImageLayout layout);

	private:
		Device& _device;

		vector<unique_ptr<FrameGraphPass>> _passes;

		// Per-frame declarations (rebuilt by the Setup sweep).
		vector<FGPassDecl> _passDecls;
		vector<FGResourceDecl> _resources;
		unordered_map<string, uint32_t> _resourceIndices; // by name

		FGCompileOutput _compiled;
		bool _compiledValid = false;

		// Physical resolution for the current frame, indexed by resource index.
		vector<Texture*> _physicalTextures;
		vector<Buffer*> _physicalBuffers;

		// The current frame's transient heap, borrowed from that frame's
		// FrameResources. Set by RealizeTransients each frame.
		TransientResourceAllocator* _currentTransients = nullptr;

		// Reset-stamp for TransientResourceAllocator.
		u64 _invalidateEpoch = 0;

		// Cross-frame state of imported resources.
		FGResourceStateRegistry _registry;

		// Per-pass contexts for the current frame.
		vector<FrameGraphPassContext> _contexts;

		// Per-frame recording state (workers read these; set before enqueue).
		// Record jobs are owned and tracked by the Threadable base.
		GpuQueueTimer* _timer = nullptr;
		uint32_t _frameIndex = 0;

		Handle<Sampler> _defaultSampler;
	};
}
