# Terrain

A quadtree-based streaming heightfield terrain, modeled after the Far Cry 5
(GDC 2018) pipeline. The CPU only decides **residency** — which quadtree nodes
live in the GPU atlases — and the GPU decides **what is drawn**: it walks the
quadtree, builds the covering set, culls patches, and issues one indirect
instanced draw for the whole world.

## World structure

The world is a grid of `rootTilesX * rootTilesZ` root tiles, each subdividing
into `lodCount` levels. **LOD 0 is the finest** (a "sector"), `lodCount - 1` the
root. A node is `quadCountPerNodeEdge` (128) quads on a side regardless of LOD,
so a coarse node covers more world per quad. Every node splits further into
`patchesPerNodeEdge^2` (8x8) **patches** — the culling and draw granularity.

| Config knob | Meaning |
|-------------|---------|
| `rootNodeSize`, `rootTilesX/Z` | World extent; `WorldOrigin()` keeps it centered on the origin |
| `lodCount`, `quadCountPerNodeEdge` | Depth of the quadtree and the vertex density of one node |
| `patchesPerNodeEdge` | Patches per node edge; **mirrored as a literal** in `terrainPatchCull.comp` (CPU-asserted) |
| `borderTexels` | Normal/albedo filtering apron so bilinear taps stay seamless across node edges |
| `atlasCapacity`, `atlasSlotsPerRow` | Atlas slot count/layout. Deliberately **smaller than the total node count**, so slot pressure and parent fallback actually happen |
| `ringRadiusScale`, `evictHysteresis` | Load ring radius (`scale * NodeSize(lod)`) and the wider radius eviction must cross |
| `uploadBudgetPerFrame` | Tile cap per frame, converted to bytes and asked of the shared upload budget |

## Frame flow

```
RenderScene::Update                       (main thread)
 └─ TerrainSystem::Update
     └─ TerrainStreamer::Update(cameraXZ)
         ├─ Pass 1  ring diff: requested vs runtime state → load list / evictions
         ├─ Pass 2  sort (coarse LOD first, then near→far), ask the shared
         │          upload budget, acquire the staging span
         ├─ Pass 3  commit admitted tiles: slot alloc + index/desc mirrors
         └─ Pass 4  ONE TerrainUploadJob (tiles + dirty tables)

FrameGraph                                (GPU, per frame)
 ├─ TerrainNodeListPass    1 workgroup: quadtree walk → Node List + indirect args
 ├─ TerrainLodMapPass      1 thread per sector: LOD map (neighbour LOD lookup)
 ├─ [DepthPre phase]
 │   ├─ HiZCull1                          (previous frame's depth pyramid)
 │   ├─ TerrainPatchCullPass  indirect, 1 group per node: 8x8 patch expand,
 │   │                        frustum + Hi-Z cull, stitch deltas → Patch List
 │   ├─ DepthPre1
 │   ├─ TerrainDepthPrePass   indirect instanced draw → depth + normal
 │   └─ Resolve → HiZCull2    (terrain is already an occluder this frame)
 └─ TerrainPass            same patches, same positions, color pass with
                           depth writes off (LESS_OR_EQUAL + `invariant`)
```

## Core types

| Type | Role |
|------|------|
| `TerrainSystem` | Facade. Owns the config, node store, quadtree and streamer; builds the shared 17x17 patch index buffer and the render params; ImGui panel |
| `TerrainConfig` | All world/bake/streaming parameters plus the derived math (`NodeSize`, `LevelOffset`, `MaxPatches`, ...). Also holds the GPU-mirrored structs |
| `TerrainHeightSource` | Where height comes from: `Sample(worldXZ)` + `CacheKey()` for bake invalidation. `ProceduralTerrainHeightSource` implements it over FBM `TerrainNoise` |
| `TerrainBaker` | Bakes one payload per node in parallel (`execution::par`): height, normal, albedo, min/max height |
| `TerrainNodeStore` | The baked CPU payloads — the in-memory stand-in for FC5's on-disk tiles. `Load()` reads the `.tbake` cache or bakes and writes it |
| `TerrainQuadTree` | GPU face: 3 synchronized atlases, the mip-mapped quadtree index texture, the node desc buffer, and the atlas slot allocator |
| `TerrainStreamer` | CPU residency: ring requests, load/evict diff, budgeted uploads, table mirrors |
| `TerrainUploadJob` | Worker-thread job that stages tile payloads + lookup tables and records the copies |

## Bake and cache

Payloads are a pure function of world position, which is what makes the world
seamless: neighbouring nodes bake **identical edge values** by construction, and
so do parent/child across LODs. Heights sit exactly on grid vertices
(`quadCountPerNodeEdge + 1` texels); normal/albedo get a `borderTexels` apron on
each side so filtering never samples a foreign slot.

`Assets/Cache/terrain_bake.tbake` is keyed by a hash of every field that changes
baked output (world dims, LOD count, texel counts, height range, height-source
key) — streaming and format knobs are excluded because they don't. The layout is
a fixed per-node stride, so `offset = header + index * stride` gives random
access: the seam where per-node disk streaming plugs in later.

## Streaming and residency

- **Request ring** — a node is requested when the camera is within
  `ringRadiusScale * NodeSize(lod)` of its XZ bounds. The **coarsest LOD is
  always requested**, so parent fallback always terminates.
- **Eviction hysteresis** — a resident node is dropped only once the camera
  leaves `evictHysteresis` times that radius, so a camera hovering on the
  boundary doesn't thrash.
- **Coarse-first ordering** — the load list sorts by LOD descending, then
  near-to-far. Refinement never breaks the fallback chain: a missing child just
  means its parent keeps covering that area.
- **Shared upload budget** — the per-frame tile cap converts to bytes and asks
  `TransferContext::GrantUploadBudget`, so a scene-load spike shrinks what
  terrain may stream that frame, and vice versa.
- **Atomic publish** — tiles and the (dirty) lookup tables go in **one** job. The
  staging span is acquired *before* any bookkeeping commits, so a full ring skips
  the whole frame rather than publishing tables that point at tiles which never
  uploaded.
- **Frame-stamped slot retire** — a released atlas slot may still be sampled by
  in-flight frames, so it re-enters the free list only after
  `MAX_FRAMES_IN_FLIGHT` frames. `AllocateSlot()` returns `TERRAIN_NODE_EMPTY`
  under pressure; the streamer counts that as `starved` and retries next frame.
- **Outside the graph** — the atlases and the index texture are written by upload
  jobs, which transition `SHADER_READ_ONLY → TRANSFER_DST → SHADER_READ_ONLY`
  (net-zero for the frame graph) and order against the frame through the transfer
  timeline. They are therefore bound directly in `Execute`, never declared as
  graph resources.

## GPU traversal

The three GPU tables the passes read are all maintained by streaming:

| Resource | Contents |
|----------|----------|
| Quadtree index texture | `R16_UINT`, one texel per node, **mip m = LOD m**. Value = atlas slot, or `TERRAIN_NODE_EMPTY` / `_INVALID` |
| Node desc buffer | One `TerrainNodeDescGPU` per atlas slot: packed min/max height + slot/LOD |
| Height / normal / albedo atlases | Synchronized — one slot index addresses all three |

**Node list** (`terrainNodeList.comp`) — a single workgroup walks the quadtree
root-down, ping-ponging the per-level lists through shared memory, so there is no
per-level dispatch or indirect round trip. The refinement predicate
`TerrainShouldRefine` (in `terrainCommon.glsl`) is **shared** with the LOD map
walk so the two can never disagree: refine when the node is inside the child ring
**and all four children are resident**. Non-refined resident nodes append to the
covering set; non-resident ones are simply skipped, because a parent already
covers them. The pass also initializes the draw args — it is the only single-
workgroup pass, so it can zero the counter race-free.

**LOD map** (`terrainLodMap.comp`) — one thread per LOD 0 sector, each walking its
own ancestor chain with the same predicate, writing the LOD of the covering node.
Gather form (per sector, not per node) keeps the work uniform where node-based
dispatch would be exponentially uneven.

**Patch cull** (`terrainPatchCull.comp`) — dispatched indirectly, one workgroup
per listed node, 64 threads = 8x8 patches. Each patch's AABB (node min/max height,
conservatively padded) is wrapped in its bounding sphere and tested against the
frustum and the previous-frame Hi-Z pyramid, reusing the mesh culler's proven
tests. Survivors append to the patch list and atomically bump `instanceCount`.

**Stitching** — the culler probes the LOD map half a sector beyond each of the
four patch edges and packs `neighbourLod - lod` as 4x4 bits. In `terrain.vert`,
an edge vertex snaps its along-edge coordinate down to a multiple of `2^delta`,
which lands exactly on a neighbour vertex (position *and* height, since the bake
is pure noise). The snapped vertices collapse into degenerate triangles that
close the T-junction — no skirts, no extra geometry.

## Drawing

One `DrawIndexedIndirect`, instance = one visible patch. There is **no vertex
buffer**: `terrain.vert` derives its position from `gl_VertexIndex` over a shared
17x17 patch grid index buffer, fetches the height with an exact `texelFetch`
(height texels sit on grid vertices), and reads its atlas slot from the patch
entry. The depth prepass and the color pass rasterize the same patches through
two pipelines; `invariant gl_Position` makes both emit bit-identical positions,
so the color pass draws early-z against the prepass depth with writes off.
Backface culling is off — a heightfield has no closed backside.

## Verification

- ImGui **"Terrain"** panel (under Renderer Passes): requested / resident /
  pending / starved node counts, tiles uploaded and evicted this frame, free
  atlas slots, and the GPU visible-patch count (read back with a 2-frame delay).
- **Freeze streaming** pins residency so you can fly out and inspect the covering
  set and the LOD ring from outside.
- **Wireframe** shows patch density and T-junctions; **Debug mode** switches
  between Lit / LOD tint / Normals / UV grid.
- CPU-side invariants (square root grid, patch/quad divisibility, 16-bit index
  space, `patchesPerNodeEdge == 8` matching the shader) are asserted at
  construction; delete `Assets/Cache/terrain_bake.tbake` to force a re-bake.
