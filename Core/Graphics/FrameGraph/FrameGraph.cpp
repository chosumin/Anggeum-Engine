#include "stdafx.h"
#include "FrameGraph.h"
#include "FrameGraphBuilder.h"
#include "FrameGraphRecordJob.h"
#include "TransientResourceAllocator.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/RenderContext.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Sampler.h"

namespace Core
{
	namespace
	{
		// Sync1 stage for SubmitInfo wait masks. The classic stage bits share
		// values with their sync2 counterparts; anything sync2-only degrades to
		// ALL_COMMANDS.
		VkPipelineStageFlags ToSync1Stage(VkPipelineStageFlags2 stage)
		{
			VkPipelineStageFlags truncated =
				static_cast<VkPipelineStageFlags>(stage & 0x7FFFFFFFull);
			if (truncated == 0 || truncated != (stage & ~0ull))
				return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			return truncated;
		}
	}

	void FrameGraphRecordJob::Execute()
	{
		_graph.RecordPass(*commandBuffer, _passIndex, _timerPassIndex);
	}

	// Defined in FrameGraphSelfTests.cpp; exercises only public APIs, so the
	// classes carry no test hooks.
	void RunFrameGraphSelfTests();

	FrameGraph::FrameGraph(Device& device, WorkerThreadManager& workerThreadManager)
		: Threadable(workerThreadManager)
		, _device(device)
	{
#ifdef _DEBUG
		static bool selfTested = false;
		if (!selfTested)
		{
			selfTested = true;
			RunFrameGraphSelfTests();
		}
#endif
	}

	FrameGraph::~FrameGraph() = default;

	void FrameGraph::AddPass(unique_ptr<FrameGraphPass> pass)
	{
		_passes.push_back(std::move(pass));
	}

	void FrameGraph::OnGUI(RenderFrame& renderFrame)
	{
		for (auto& pass : _passes)
			pass->OnGUI(renderFrame);
	}

	void FrameGraph::Invalidate()
	{
		// The transient resets lazily on its next realize.
		_invalidateEpoch++;
		_compiledValid = false;
	}

	uint32_t FrameGraph::DeclareResource(FGResourceDecl&& decl)
	{
		uint32_t index = static_cast<uint32_t>(_resources.size());
		_resourceIndices.emplace(decl.name, index);
		_resources.push_back(std::move(decl));
		return index;
	}

	// ============================================================
	// Compile core (device-independent)
	// ============================================================
	void FrameGraph::CompileDeclarations(const vector<FGPassDecl>& passes,
		const vector<FGResourceDecl>& resources,
		const vector<FGResourceState>& entryStates,
		FGCompileOutput& out)
	{
		const size_t passCount = passes.size();
		const size_t resourceCount = resources.size();

		out.culledPasses.assign(passCount, 0);
		out.lifetimes.assign(resourceCount, FGLifetime{});
		out.passSync.assign(passCount, FGPassSync{});
		out.preBarriers.assign(passCount, {});
		out.finalStates.assign(resourceCount, FGResourceState{});

		// ---- Pass culling: refcount on reads, iterated to a fixpoint ----
		vector<uint32_t> refCount(resourceCount, 0);
		for (size_t i = 0; i < resourceCount; i++)
		{
			// Imported resources have consumers outside the graph.
			if (resources[i].imported)
				refCount[i]++;
		}
		for (const auto& pass : passes)
			for (const auto& access : pass.accesses)
				if (!access.info.isWrite)
					refCount[access.resource]++;

		bool changed = true;
		while (changed)
		{
			changed = false;
			for (size_t i = 0; i < passCount; i++)
			{
				if (out.culledPasses[i] || passes[i].sideEffect)
					continue;

				bool anyNeededWrite = false;
				for (const auto& access : passes[i].accesses)
					if (access.info.isWrite && refCount[access.resource] > 0)
						anyNeededWrite = true;

				if (!anyNeededWrite)
				{
					// Cull this pass: reduce the refcount, so that upstream writes can be culled too.
					out.culledPasses[i] = 1;
					changed = true;
					for (const auto& access : passes[i].accesses)
						if (!access.info.isWrite)
							refCount[access.resource]--;
				}
			}
		}

		// ---- Lifetimes, hazard tracking, barrier planning, cross-queue deps ----
		struct Track
		{
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkPipelineStageFlags2 lastWriteStage = VK_PIPELINE_STAGE_2_NONE;
			VkAccessFlags2 lastWriteAccess = VK_ACCESS_2_NONE;
			VkPipelineStageFlags2 readStages = VK_PIPELINE_STAGE_2_NONE;
			VkPipelineStageFlags2 visibleStages = VK_PIPELINE_STAGE_2_NONE;
			VkAccessFlags2 visibleAccess = VK_ACCESS_2_NONE;
			int32_t lastAccessPass = -1;
		};
		vector<Track> tracks(resourceCount);
		for (size_t r = 0; r < resourceCount; r++)
		{
			if (resources[r].imported)
			{
				tracks[r].layout = entryStates[r].layout;
				tracks[r].lastWriteStage = entryStates[r].stage;
				tracks[r].lastWriteAccess = entryStates[r].access;
			}
			// Transients start UNDEFINED with no producer.
		}

		for (size_t i = 0; i < passCount; i++)
		{
			if (out.culledPasses[i])
				continue;

			const QueueType myQueue = passes[i].queue;

			for (const auto& access : passes[i].accesses)
			{
				const uint32_t resourceIndex = access.resource;
				const FGAccessInfo& accessInfo = access.info;
				const bool isImage = resources[resourceIndex].isTexture;
				Track& track = tracks[resourceIndex];

				auto& lifetime = out.lifetimes[resourceIndex];
				lifetime.used = true;
				lifetime.firstPass = std::min(lifetime.firstPass, static_cast<uint32_t>(i));
				lifetime.lastPass = std::max(lifetime.lastPass, static_cast<uint32_t>(i));

				// Cross-queue dependency: pass order does not order execution
				// across queues, so this pass must wait on the producer pass's
				// timeline signal.
				const bool crossQueue = track.lastAccessPass >= 0 &&
					passes[track.lastAccessPass].queue != myQueue;
				if (crossQueue)
				{
					FGPassSync& sync = out.passSync[i];
					sync.waitPass = std::max(sync.waitPass, track.lastAccessPass);
					sync.waitStages |= ToSync1Stage(accessInfo.stage);
					lifetime.crossQueue = true;
				}

				// Manual-barrier accesses: the pass transitions the resource
				// itself; the graph only adopts the declared final state.
				if (accessInfo.manualBarriers)
				{
					if (isImage)
						track.layout = accessInfo.layout;
					track.lastWriteStage = accessInfo.stage;
					track.lastWriteAccess = accessInfo.access;
					track.readStages = VK_PIPELINE_STAGE_2_NONE;
					track.visibleStages = VK_PIPELINE_STAGE_2_NONE;
					track.visibleAccess = VK_ACCESS_2_NONE;
					track.lastAccessPass = static_cast<int32_t>(i);
					continue;
				}

				// Does this access need a barrier?
				bool needBarrier = false;
				if (isImage && accessInfo.layout != track.layout)
					needBarrier = true;
				else if (accessInfo.isWrite)
					needBarrier = true;
				else
				{
					const bool visible =
						(accessInfo.stage & ~track.visibleStages) == 0 &&
						(accessInfo.access & ~track.visibleAccess) == 0;
					needBarrier = !visible;
				}

				if (needBarrier)
				{
					FGBarrierPlan plan;
					plan.resource = resourceIndex;
					plan.isImage = isImage;
					if (crossQueue)
					{
						// Execution order comes from the timeline wait; the
						// semaphore also makes prior writes available, so the
						// barrier only performs the layout transition.
						plan.srcStage = VK_PIPELINE_STAGE_2_NONE;
						plan.srcAccess = VK_ACCESS_2_NONE;
					}
					else
					{
						plan.srcStage = track.lastWriteStage |
							(accessInfo.isWrite ? track.readStages : VK_PIPELINE_STAGE_2_NONE);
						plan.srcAccess = track.lastWriteAccess;
					}
					plan.dstStage = accessInfo.stage;
					plan.dstAccess = accessInfo.access;
					if (isImage)
					{
						plan.oldLayout = track.layout;
						plan.newLayout = accessInfo.layout;
					}
					out.preBarriers[i].push_back(plan);

					// A layout change invalidates any previously established
					// visibility of the old contents.
					if (isImage && plan.oldLayout != plan.newLayout)
					{
						track.visibleStages = VK_PIPELINE_STAGE_2_NONE;
						track.visibleAccess = VK_ACCESS_2_NONE;
					}
				}

				// State update.
				if (accessInfo.isWrite)
				{
					if (isImage)
						track.layout = accessInfo.layout;
					track.lastWriteStage = accessInfo.stage;
					track.lastWriteAccess = accessInfo.access;
					track.readStages = VK_PIPELINE_STAGE_2_NONE;
					track.visibleStages = VK_PIPELINE_STAGE_2_NONE;
					track.visibleAccess = VK_ACCESS_2_NONE;
				}
				else
				{
					if (isImage)
						track.layout = accessInfo.layout;
					track.readStages |= accessInfo.stage;
					track.visibleStages |= accessInfo.stage;
					track.visibleAccess |= accessInfo.access;
				}
				track.lastAccessPass = static_cast<int32_t>(i);
			}
		}

		// ---- Mark producer passes that must signal their timeline ----
		for (const auto& sync : out.passSync)
			if (sync.waitPass >= 0)
				out.passSync[sync.waitPass].signals = true;

		// ---- Final states: fed back to the registry for imports (next frame's
		// entry) ---- the last access already left the image in this layout, so
		// no forced export barrier is needed.
		for (size_t i = 0; i < resourceCount; i++)
		{
			if (!out.lifetimes[i].used)
				continue;

			const Track& track = tracks[i];
			out.finalStates[i].layout = track.layout;
			out.finalStates[i].stage = track.lastWriteStage | track.readStages;
			out.finalStates[i].access = track.lastWriteAccess;
		}
	}

	// ============================================================
	// Per-frame driver (main thread)
	// ============================================================
	void FrameGraph::SetupAndCompile(RenderFrame& renderFrame, uint32_t imageIndex)
	{
		_passDecls.clear();
		_resources.clear();
		_resourceIndices.clear();
		_compiledValid = false;

		if (_passes.empty())
			return;

		// Setup sweep: every pass declares its resources for this frame.
		_passDecls.resize(_passes.size());
		for (size_t p = 0; p < _passes.size(); p++)
		{
			_passDecls[p].pass = _passes[p].get();
			_passDecls[p].queue = _passes[p]->GetQueueType();

			FrameGraphBuilder builder(*this, static_cast<uint32_t>(p));
			_passes[p]->Setup(builder, renderFrame.GetResources(), renderFrame);
		}

		vector<FGResourceState> entryStates;
		CollectEntryStates(entryStates);

		// Compiled every frame: the compile is linear in the declarations (a
		// change-detection hash would cost the same order), and the expensive
		// part — transient realization — has its own unchanged-request skip.
		CompileDeclarations(_passDecls, _resources, entryStates, _compiled);

		RealizeTransients(renderFrame);
		ResolvePhysical();
		BuildPassContexts(renderFrame, imageIndex);

		_compiledValid = true;
	}

	void FrameGraph::CollectEntryStates(vector<FGResourceState>& outEntryStates)
	{
		outEntryStates.assign(_resources.size(), FGResourceState{});

		for (size_t r = 0; r < _resources.size(); r++)
		{
			auto& decl = _resources[r];
			if (!decl.imported)
				continue;

			if (decl.isTexture)
			{
				Texture* texture = decl.importedTexture.TryGet();
				assert(texture != nullptr && "imported texture handle is dead");

				// The registry is the sole entry-state authority: what last frame
				// left the image in. Empty on the first frame -> UNDEFINED, which
				// is a discard (correct, since every import's first access writes).
				outEntryStates[r] = _registry.GetTextureState(*texture, FGResourceState{});
			}
			else
			{
				Buffer* buffer = decl.importedBuffer.TryGet();
				assert(buffer != nullptr && "imported buffer handle is dead");
				outEntryStates[r] = _registry.GetBufferState(*buffer);
			}
		}
	}

	void FrameGraph::RealizeTransients(RenderFrame& renderFrame)
	{
		if (!_defaultSampler.IsValid())
			_defaultSampler = _device.GetResourceManager().LoadSampler(DEFAULT_SAMPLER);

		auto& transients = renderFrame.GetResources().GetTransientAllocator();

		// Lazy invalidation (resize etc.): this slot has not been realized since
		// the last Invalidate().
		if (transients.GetInvalidateEpoch() != _invalidateEpoch)
		{
			transients.Reset();
			transients.SetInvalidateEpoch(_invalidateEpoch);
		}

		// Collect the surviving transients with their lifetimes; the allocator
		// aliases resources whose pass intervals do not overlap.
		vector<TransientResourceAllocator::Request> requests;
		vector<uint32_t> transientResources;

		for (size_t i = 0; i < _resources.size(); i++)
		{
			auto& resource = _resources[i];
			const auto& lifetime = _compiled.lifetimes[i];
			if (resource.imported || !lifetime.used)
				continue;

			TransientResourceAllocator::Request request;
			request.name = resource.name;
			request.isTexture = resource.isTexture;
			request.texDesc = resource.texDesc;
			request.bufDesc = resource.bufDesc;
			request.firstPass = lifetime.firstPass;
			request.lastPass = lifetime.lastPass;
			request.queue = _passDecls[lifetime.firstPass].queue;

			// Multi-queue resources cannot alias at all: pass indices do not
			// order execution across queues, so inflate the interval to the
			// whole frame (blocks every memory-sharing candidate).
			if (lifetime.crossQueue)
			{
				request.firstPass = 0;
				request.lastPass = static_cast<uint32_t>(_passDecls.size() - 1);
			}

			requests.push_back(std::move(request));
			transientResources.push_back(static_cast<uint32_t>(i));
		}

		transients.Realize(requests, _defaultSampler);

		// Aliasing activation: when two transients share memory, the
		// later-starting one must wait for the earlier one's final accesses
		// before its (already UNDEFINED-discarding) first barrier.
		const auto& placements = transients.GetPlacements();
		for (size_t i = 0; i < placements.size(); i++)
		{
			for (size_t j = i + 1; j < placements.size(); j++)
			{
				const bool memoryOverlap =
					placements[i].offset < placements[j].offset + placements[j].size &&
					placements[j].offset < placements[i].offset + placements[i].size;
				if (!memoryOverlap)
					continue;

				const uint32_t resourceA = transientResources[i];
				const uint32_t resourceB = transientResources[j];
				const bool aFirst = _compiled.lifetimes[resourceA].lastPass <
					_compiled.lifetimes[resourceB].firstPass;
				const uint32_t earlier = aFirst ? resourceA : resourceB;
				const uint32_t later = aFirst ? resourceB : resourceA;

				auto& barriers = _compiled.preBarriers[_compiled.lifetimes[later].firstPass];
				
				bool patched = false;

				for (auto& plan : barriers)
				{
					if (plan.resource != later)
						continue;

					assert(plan.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED || !plan.isImage);
					
					plan.srcStage |= _compiled.finalStates[earlier].stage;
					patched = true;
					break;
				}
				assert(patched && "aliased transient has no first-use barrier to patch");
			}
		}

		_currentTransients = &transients;
	}

	void FrameGraph::ResolvePhysical()
	{
		auto& transients = *_currentTransients;

		_physicalTextures.assign(_resources.size(), nullptr);
		_physicalBuffers.assign(_resources.size(), nullptr);

		for (size_t r = 0; r < _resources.size(); r++)
		{
			auto& resource = _resources[r];
			if (!_compiled.lifetimes[r].used)
				continue;

			if (resource.isTexture)
			{
				if (resource.imported)
					_physicalTextures[r] = resource.importedTexture.TryGet();
				else
					_physicalTextures[r] = transients.GetTexture(resource.name);

				assert(_physicalTextures[r] != nullptr);
			}
			else
			{
				if (resource.imported)
					_physicalBuffers[r] = resource.importedBuffer.TryGet();
				else
					_physicalBuffers[r] = transients.GetBuffer(resource.name);

				assert(_physicalBuffers[r] != nullptr);
			}
		}
	}

	void FrameGraph::BuildPassContexts(RenderFrame& renderFrame, uint32_t imageIndex)
	{
		// Reference members: contexts are constructed in place (no resize) and
		// reserve() keeps the RenderingSetup self-pointers stable while filling.
		_contexts.clear();
		_contexts.reserve(_passDecls.size());

		for (size_t p = 0; p < _passDecls.size(); p++)
		{
			auto& passDecl = _passDecls[p];

			_contexts.push_back(FrameGraphPassContext(_device,
				renderFrame.GetResources().GetDescriptorPool(),
				renderFrame, imageIndex,
				_physicalTextures, _physicalBuffers));
			auto& context = _contexts.back();

			context._declared.assign(_resources.size(), 0);
			for (const auto& access : passDecl.accesses)
				context._declared[access.resource] = 1;

			if (_compiled.culledPasses[p])
				continue;

			for (uint32_t variant = 0; variant < FrameGraphPassContext::MaxRenderingVariants; variant++)
			{
				if (!passDecl.hasRendering[variant])
					continue;

				auto& setup = context._rendering[variant];
				setup.valid = true;

				VkExtent2D renderArea{ 0, 0 };

				for (const auto& attachment : passDecl.colorAttachments[variant])
				{
					Texture* texture = _physicalTextures[attachment.texture.index];
					assert(texture != nullptr);

					VkRenderingAttachmentInfo attachmentInfo{};
					attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
					attachmentInfo.imageView = texture->GetImageView();
					attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					attachmentInfo.loadOp = attachment.loadOp;
					attachmentInfo.storeOp = attachment.storeOp;
					attachmentInfo.clearValue = attachment.clear;

					if (attachment.resolveTarget.IsValid())
					{
						Texture* resolve = _physicalTextures[attachment.resolveTarget.index];
						assert(resolve != nullptr);
						attachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
						attachmentInfo.resolveImageView = resolve->GetImageView();
						attachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					}

					setup.colorAttachments.push_back(attachmentInfo);

					const auto& extent = texture->GetExtent();
					renderArea = { extent.width, extent.height };
				}

				if (passDecl.hasDepth[variant])
				{
					const auto& attachment = passDecl.depthAttachments[variant];
					Texture* texture = _physicalTextures[attachment.texture.index];
					assert(texture != nullptr);

					auto& attachmentInfo = setup.depthAttachment;
					attachmentInfo = {};
					attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
					attachmentInfo.imageView = texture->GetImageView();
					attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					attachmentInfo.loadOp = attachment.loadOp;
					attachmentInfo.storeOp = attachment.storeOp;
					attachmentInfo.clearValue = attachment.clear;
					setup.hasDepth = true;

					const auto& extent = texture->GetExtent();
					renderArea = { extent.width, extent.height };
				}

				auto& renderingInfo = setup.renderingInfo;
				renderingInfo = {};
				renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
				renderingInfo.renderArea = { { 0, 0 }, renderArea };
				renderingInfo.layerCount = 1;
				renderingInfo.colorAttachmentCount = static_cast<uint32_t>(setup.colorAttachments.size());
				renderingInfo.pColorAttachments = setup.colorAttachments.data();
				renderingInfo.pDepthAttachment = setup.hasDepth ? &setup.depthAttachment : nullptr;
			}
		}
	}

	// ============================================================
	// Recording + submission (single-threaded for now)
	// ============================================================
	namespace
	{
		void RecordBarriers(CommandBuffer& commandBuffer,
			const vector<FGBarrierPlan>& plans,
			const vector<Texture*>& textures,
			const vector<Buffer*>& buffers)
		{
			if (plans.empty())
				return;

			BarrierBatch batch = commandBuffer.CreateBarrierBatch();
			for (const auto& plan : plans)
			{
				if (plan.isImage)
				{
					batch.Image(*textures[plan.resource], plan.oldLayout, plan.newLayout,
						plan.srcStage, plan.srcAccess, plan.dstStage, plan.dstAccess);
				}
				else
				{
					batch.Buffer(*buffers[plan.resource],
						plan.srcStage, plan.srcAccess, plan.dstStage, plan.dstAccess);
				}
			}
			batch.Submit();
		}
	}

	void FrameGraph::RecordPass(CommandBuffer& commandBuffer, uint32_t passIndex, uint32_t timerPassIndex)
	{
		auto& decl = _passDecls[passIndex];
		auto& context = _contexts[passIndex];

		_timer->WriteBeginTimestamp(commandBuffer, _frameIndex, timerPassIndex);
		commandBuffer.BeginDebugMarker(decl.pass->GetName());

		// All barriers belong here, ahead of Execute: a pass that renders opens its
		// own scope inside Execute, and barriers are illegal inside one.
		RecordBarriers(commandBuffer, _compiled.preBarriers[passIndex],
			_physicalTextures, _physicalBuffers);

		decl.pass->Execute(context, commandBuffer);

		commandBuffer.EndDebugMarker();
		_timer->EndPass(commandBuffer, _frameIndex, timerPassIndex);
	}

	uint32_t FrameGraph::Execute(RenderContext& renderContext, RenderFrame& renderFrame)
	{
		if (!_compiledValid)
			return 0;

		auto& queueTimer = renderContext.GetQueueTimer();
		auto& syncContext = renderContext.GetSyncContext();
		_timer = &queueTimer;
		_frameIndex = renderContext.GetCurrentFrameIndex();

		ClearJobs();

		// One record job per alive pass, in declaration order.
		// Recording window: past this point the main thread only enqueues and
		// waits, so the resource pools are frozen and worker-side Handle<T>::Get() is safe.
		FrameResources::SetRecordingGuard(true);

		uint32_t timerPassIndex = 0;
		for (size_t i = 0; i < _passDecls.size(); i++)
		{
			if (_compiled.culledPasses[i])
				continue;

			auto& decl = _passDecls[i];
			queueTimer.RegisterPass(_frameIndex, timerPassIndex, decl.queue, decl.pass->GetName());

			Enqueue(make_unique<FrameGraphRecordJob>(*this,
				static_cast<uint32_t>(i), timerPassIndex, decl.queue));
			timerPassIndex++;
		}

		WaitForJobs();

		FrameResources::SetRecordingGuard(false);

		// Submit infos in declaration order, carrying the compiled cross-queue
		// timeline waits/signals per pass.
		size_t jobIndex = 0;
		for (size_t i = 0; i < _passDecls.size(); i++)
		{
			if (_compiled.culledPasses[i])
				continue;

			auto& decl = _passDecls[i];
			auto& sync = _compiled.passSync[i];

			SubmitInfo& submitInfo = renderFrame.AddSubmitInfo(decl.queue,
				GetJob(jobIndex)->commandBuffer->GetHandle(), syncContext);

			if (sync.waitPass >= 0)
			{
				const FGPassSync& producer = _compiled.passSync[sync.waitPass];
				assert(producer.signalValue != 0 && "producer signal not assigned yet");
				submitInfo.AddTimelineWaitSemaphore(
					syncContext.GetSemaphore(_passDecls[sync.waitPass].queue),
					producer.signalValue,
					sync.waitStages != 0 ? sync.waitStages : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
			}

			// Signal right after the producing pass (not at the end of its queue
			// run), so cross-queue consumers can start as early as possible.
			if (sync.signals)
			{
				submitInfo.AddSignalSemaphore(decl.queue);
				sync.signalValue = syncContext.GetCurrentValue(decl.queue);
			}

			jobIndex++;
		}

		UpdateRegistry();

		return timerPassIndex;
	}

	void FrameGraph::UpdateRegistry()
	{
		for (size_t r = 0; r < _resources.size(); r++)
		{
			if (!_compiled.lifetimes[r].used || !_resources[r].imported)
				continue;

			if (_resources[r].isTexture)
			{
				Texture* texture = _physicalTextures[r];
				if (texture != nullptr)
					_registry.SetTextureState(*texture, _compiled.finalStates[r]);
			}
			else
			{
				Buffer* buffer = _physicalBuffers[r];
				if (buffer != nullptr)
					_registry.SetBufferState(*buffer, _compiled.finalStates[r]);
			}
		}
	}

	// ============================================================
	// Debugging
	// ============================================================
	string FrameGraph::DumpCompiled() const
	{
		std::ostringstream os;

		os << "FrameGraph: " << _passDecls.size() << " passes, "
			<< _resources.size() << " resources\n";

		for (size_t p = 0; p < _passDecls.size(); p++)
		{
			if (_compiled.culledPasses[p])
				continue;

			const auto& sync = _compiled.passSync[p];

			os << " pass " << p << " [" <<
				(_passDecls[p].queue == QueueType::Compute ? "compute" : "graphics") <<
				"] '" << _passDecls[p].pass->GetName() << "'";
			if (sync.waitPass >= 0)
				os << " waits(pass " << sync.waitPass << ")";
			if (sync.signals)
				os << " signals";
			os << "\n";

			for (const auto& plan : _compiled.preBarriers[p])
			{
				os << "  pre  " << _resources[plan.resource].name;
				if (plan.isImage)
					os << " layout " << plan.oldLayout << " -> " << plan.newLayout;
				os << "\n";
			}
		}

		for (size_t p = 0; p < _passDecls.size(); p++)
			if (_compiled.culledPasses[p])
				os << " culled: '" << _passDecls[p].pass->GetName() << "'\n";

		return os.str();
	}

	void FrameGraph::OnDebugGUI() const
	{
		if (!ImGui::CollapsingHeader("Frame Graph"))
			return;

		if (!_compiledValid)
		{
			ImGui::TextUnformatted("(not compiled)");
			return;
		}

		for (size_t p = 0; p < _passDecls.size(); p++)
		{
			if (_compiled.culledPasses[p])
				continue;

			const auto& sync = _compiled.passSync[p];

			ImGui::Text("%s [%s]%s  (%zu barriers)",
				_passDecls[p].pass->GetName(),
				_passDecls[p].queue == QueueType::Compute ? "Compute" : "Graphics",
				sync.signals ? " (signals)" : "",
				_compiled.preBarriers[p].size());

			if (sync.waitPass >= 0)
				ImGui::Text("   waits on %s",
					_passDecls[sync.waitPass].pass->GetName());
		}

		for (size_t p = 0; p < _passDecls.size(); p++)
			if (_compiled.culledPasses[p])
				ImGui::Text("Culled: %s", _passDecls[p].pass->GetName());

		if (_currentTransients != nullptr)
		{
			const auto& transients = *_currentTransients;
			ImGui::Text("Transient heap: %.2f MB",
				static_cast<double>(transients.GetHeapSize()) / (1024.0 * 1024.0));

			const auto& requests = transients.GetRequests();
			const auto& placements = transients.GetPlacements();
			for (size_t i = 0; i < requests.size(); i++)
			{
				ImGui::Text("   %s  offset %llu  size %.2f MB  passes %u-%u",
					requests[i].name.c_str(),
					static_cast<unsigned long long>(placements[i].offset),
					static_cast<double>(placements[i].size) / (1024.0 * 1024.0),
					requests[i].firstPass, requests[i].lastPass);
			}
		}
	}

}
