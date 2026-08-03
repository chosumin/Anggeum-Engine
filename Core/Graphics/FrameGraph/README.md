# Frame Graph

A render pass framework where passes **declare** their resource reads/writes and
the compile phase derives everything else automatically — pass culling, lifetime
analysis, sync2 barriers, cross-queue timeline synchronization, and memory
aliasing — while recording is parallelized across worker threads. Built on
Vulkan 1.4 + dynamic rendering.

## Frame flow

```
RenderPipeline::Draw
 ├─ FrameGraph::SetupAndCompile   (main thread)
 │   ├─ Setup sweep: each pass's Setup(builder) — resource declarations
 │   ├─ Compile: culling → barrier planning → per-pass cross-queue sync points
 │   ├─ RealizeTransients: aliased placement + offset bind (skipped when the request set is unchanged)
 │   └─ BuildPassContexts: physical resource resolution + prebuilt RenderingInfo
 ├─ FrameGraph::Execute
      ├─ One RecordJob per pass → recorded in parallel on WorkerThreads
      ├─ Main thread waits until every job completes
      └─ SubmitInfos registered in declaration order
```

## Core types

| Type | Role |
|------|------|
| `FrameGraphPass` | Pass base class. `Setup(builder, frameResources, renderFrame)` (main thread, declarations) + `Execute()` (worker, recording) |
| `FrameGraphBuilder` | Passed to Setup. Create/Import/Read/Write/attachment declarations |
| `FrameGraphPassContext` | Passed to Execute. Virtual handle → physical resource, `BeginRendering()`, `GetRenderFrame()` |
| `TransientResourceAllocator` | Aliasing heap. Greedy interval placement. Owned by `FrameResources` as per-slot storage, driven by the graph's compile output |
| `FGResourceStateRegistry` | Cross-frame layout/stage/access ledger for imported resources |

## Transient vs Import

- **Transient** (`builder.CreateTexture/CreateBuffer`): graph-owned. Resources
  whose lifetime intervals don't overlap and that stay on the **same queue**
  alias the same memory. Starts UNDEFINED every frame.
  Candidates are RTs **with no history whose consumers are all inside the graph**:
  - **history resources must never be transient**
- **Import** (`builder.ImportTexture(name, handle)`): anything that needs history
  or is consumed outside the graph:
  - **The `FGResourceStateRegistry` is the sole authority on entry state** — the
    resource enters the frame in exactly the state (layout/stage/access) the
    previous frame left it in. This is correct even when an import's first
    access of the frame is a discarding write.
  - **No export barriers** — the layout left by the last graph access simply is
    the final state; `UpdateRegistry` writes it into the registry, and it
    becomes the next frame's entry state.

## Verification

- The compile core + placement self-tests run automatically on the first
  FrameGraph construction in debug builds.
- Inspect batches/barriers/culling/transient heap offsets via `DumpCompiled()`
  or the "Frame Graph" section of the ImGui "Renderer Passes" window.
- Validate barrier correctness with the validation layers (+sync validation).
