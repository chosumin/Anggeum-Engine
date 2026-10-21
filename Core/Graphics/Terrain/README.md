# Terrain

A quadtree-based streaming heightfield terrain, modeled after the Far Cry 5
(GDC 2018) pipeline. The CPU only decides **residency** — which quadtree nodes
live in the GPU atlases — and the GPU decides **what is drawn**: it walks the
quadtree, builds the covering set, culls patches, and issues one indirect
instanced draw per list for the whole world.

Terrain has no passes of its own beyond the two traversal passes. Its culls and
draws are folded into the mesh passes (Hi-Z cull, depth prepass, shadow cull and
draw, geometry pass) through two helpers, `TerrainPatchCuller` and
`TerrainRenderer`, so terrain and meshes share one depth pyramid, one depth
prepass, one shadow map and one color pass.

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
| `splatQuadsPerTexel` | Splat (material ID) map density: one texel every N grid vertices, edges shared (65 per node edge) |
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
         │          (nodes enter PendingUpload)
         └─ Pass 4  ONE TerrainUploadJob (tiles + dirty tables); when the
                    transfer lands, onLanded promotes the nodes to Resident

FrameGraph                                (GPU, per frame)
 ├─ TerrainNodeListPass  1 workgroup: quadtree walk → Node List + indirect args
 ├─ TerrainLodMapPass    1 thread per sector: LOD map (neighbour LOD lookup)
 ├─ HiZCull1             pass-1 cull: 8x8 patch expand, frustum + previous-frame
 │                       Hi-Z → Patch List; occlusion rejects → Rejected List
 ├─ DepthPre1            pass-1 list → depth + normal
 ├─ Resolve → HiZCull2   pass-2 cull: rejects re-tested against this frame's
 │                       pyramid (terrain is already an occluder) → Pass2 list
 ├─ DepthPre2            pass-2 list → depth + normal
 ├─ ShadowCull           per cascade: frustum-only cull of the same node list
 │                       → cascade Patch List
 ├─ ShadowPass           per cascade: depth-only draw of the cascade list
 ├─ SDFShadowPass        far shadow mask: mesh SDF march + terrain heightfield
 │                       march (TerrainSampleHeight over the quadtree index)
 └─ OpaquePass         both lists, color with depth writes off
```

## Core types

| Type | Role |
|------|------|
| `TerrainSystem` | Facade. Owns the config, node store, quadtree and streamer; builds the shared 17x17 patch index buffer and the render params; ImGui panel and mouse pick |
| `TerrainConfig` | All world/bake/streaming parameters plus the derived math (`NodeSize`, `LevelOffset`, `MaxPatches`, ...). Also holds the GPU-mirrored structs |
| `TerrainHeightSource` | Where height comes from: `Sample(worldXZ)` + `CacheKey()` for bake invalidation. `ProceduralTerrainHeightSource` implements it over FBM `TerrainNoise` |
| `TerrainBaker` | Bakes one payload per node in parallel (`execution::par`): height, normal, albedo, splat (material ID), min/max height |
| `TerrainNodeStore` | The baked CPU payloads — the in-memory stand-in for FC5's on-disk tiles. `Load()` reads the `.tbake` cache or bakes and writes it |
| `TerrainQuadTree` | GPU face: 4 synchronized atlases (height; normal + roughness + AO; albedo; splat material IDs), the mip-mapped quadtree index texture, the node desc buffer, and the atlas slot allocator |
| `TerrainStreamer` | CPU residency: ring requests, load/evict diff, budgeted uploads, table mirrors, PendingUpload → Resident promotion |
| `TerrainUploadJob` | Worker-thread job that stages tile payloads + lookup tables and records the copies |
| `TerrainMaterialDesc` / `TerrainMaterialBakeDesc` | One palette entry split by consumer: the runtime side (`TerrainMaterial.h`: name, tile size/rotation, the GPU palette structs) and the bake side (`TerrainBaker.h`: the albedo/roughness the atlases average to). Ready-made tiles never need the bake side |
| `TerrainMaterialSet` | The runtime palette: the descriptors, the detail texture handles behind them (registered bindless by whoever produced them) and the GPU palette they resolve to |
| `ProceduralTerrainMaterials` | The procedural producer: the biome palette (`TerrainProceduralMaterial` = runtime desc + bake desc + noise look), its bake descs for the node bake, and `CreateProceduralTerrainMaterials()`, which bakes the detail maps with `TerrainDetailBaker`, uploads them once (`TerrainDetailUploadJob`) and hands the set the handles |
| `TerrainPatchCuller` | The cull dispatches a host pass embeds: declares the node list / LOD map inputs and the list / args / rejected outputs, resets args, dispatches pass 1 (`terrainPatchCull.comp`) and pass 2 (`terrainPatchCullPass2.comp`) |
| `TerrainRenderer` | The draws a host pass embeds: depth prepass (per phase), color (both lists), shadow (per cascade). Owns the three pipelines; the host owns the attachments |

Host passes: `HiZCullPass` and `ShadowCullPass` call the culler; `DepthPrePass`,
`OpaquePass` and `ShadowPass` call the renderer. Each host declares the terrain
side in its `Setup` and records it in its `Execute` right next to the mesh work.
The helpers return false / early-out when their list does not exist that frame,
so a host never branches on terrain itself. Where a host exists once per phase
(`HiZCullPass`, `DepthPrePass`) it keeps a single list; the shared
`TerrainRenderer` keeps one list per phase and one per cascade.

## Bake and cache

Payloads are a pure function of world position, which is what makes the world
seamless: neighbouring nodes bake **identical edge values** by construction, and
so do parent/child across LODs. Heights sit exactly on grid vertices
(`quadCountPerNodeEdge + 1` texels); normal/albedo get a `borderTexels` apron on
each side so filtering never samples a foreign slot.

The bake takes the palette's far side, one `TerrainMaterialBakeDesc` per
material. The biome rules (height, slope) produce **weights** over the
palette, the atlas albedo/roughness blend the entries with those weights, and
the **splat map** draws one material ID per texel from the same weights
(stochastic pick, hashed from the world position so every LOD draws the same
ID at the same point - a 50/50 zone bakes a dithered mix of both IDs for the
near shader to blend). Splat texels sit on every `splatQuadsPerTexel`-th grid
vertex (65 per edge), edges shared like heights. Bit 7 of the ID is reserved
for the Phase 4 cliff flag.

The detail maps are baked too, per material rather than per node, and not
cached: `TerrainDetailBaker` builds tileable noise with a full mip chain,
with the albedo centered on the bake albedo so near and far average to the
same color. They are uploaded once (`TerrainDetailUploadJob`, an init job)
and registered with the bindless set like any file texture.

`Assets/Cache/terrain_bake.tbake` is keyed by a hash of every field that changes
baked output (world dims, LOD count, texel counts, height range, height-source
key, the far side of the palette) — streaming and format knobs are excluded because they don't. The layout is
a fixed per-node stride, so `offset = header + index * stride` gives random
access: the seam where per-node disk streaming plugs in later.

## Streaming and residency

- **Request ring** — a node is requested when the camera is within
  `ringRadiusScale * NodeSize(lod)` of its XZ bounds. The **coarsest LOD is
  always requested**, so parent fallback always terminates.
- **Eviction hysteresis** — a resident node is dropped only once the camera
  leaves `evictHysteresis` times that radius, so a camera hovering on the
  boundary doesn't thrash. Only `Resident` nodes are evicted; a node still in
  flight keeps its slot until it lands.
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
- **Promotion-gated residency** — admitted nodes are `PendingUpload` until the
  transfer queue reports the job landed (`PendingUpload::onLanded` →
  `PromoteNode`), and only then become `Resident` and enter the GPU tables the
  traversal reads. There is no CPU wait on the transfer; frames that render
  before the landing simply see the parent.
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
| Height / normal / albedo / splat atlases | Synchronized — one slot index addresses all four |

**Node list** (`terrainNodeList.comp`) — a single workgroup walks the quadtree
root-down, ping-ponging the per-level lists through shared memory, so there is no
per-level dispatch or indirect round trip. The refinement predicate
`TerrainShouldRefine` (in `terrainCommon.glsl`) is **shared** with the LOD map
walk so the two can never disagree: refine when the node is inside the child ring
**and all four children are resident**. Non-refined resident nodes append to the
covering set; non-resident ones are simply skipped, because a parent already
covers them. The pass also initializes the pass-1 draw args — it is the only
single-workgroup pass, so it can zero the counter race-free.

**LOD map** (`terrainLodMap.comp`) — one thread per LOD 0 sector, each walking its
own ancestor chain with the same predicate, writing the LOD of the covering node.
Gather form (per sector, not per node) keeps the work uniform where node-based
dispatch would be exponentially uneven.

## Culling

Patches are culled as **AABBs** with the same `IsVisibleFrustum` / `IsOccluded`
the mesh culler uses (`gpuDriven.glsl`); `TerrainPatchAABB` (`terrainCommon.glsl`)
builds the box from the packed node coordinate, the patch index and the node's
min/max height, conservatively padded by `heightBounds.z`. Three dispatches share
this:

- **Pass 1** (`terrainPatchCull.comp`, inside `HiZCull1`) — indirect, one
  workgroup per listed node, 64 threads = 8x8 patches. Frustum first, then the
  **previous frame's** Hi-Z. Survivors append to the pass-1 patch list; patches
  that failed only the occlusion test append to the **rejected list** instead.
  Stitch deltas (below) are computed before the tests so a rejected entry carries
  them into pass 2.
- **Pass 2** (`terrainPatchCullPass2.comp`, inside `HiZCull2`) — one thread per
  rejected entry, re-tested against **this frame's** pyramid, which already
  contains the pass-1 terrain and meshes. Survivors form the pass-2 list with its
  own draw args. A disocclusion therefore costs one frame of latency at most,
  never a hole.
- **Shadow** (pass-1 shader, inside `ShadowCullPass`) — the same node list, once
  per cascade against the cascade's frustum only (`screenHiZ.w = 0` disables the
  Hi-Z test). Each cascade gets its own list and args, read by `ShadowPass`.

**Stitching** — the culler probes the LOD map one patch length beyond each of
the four patch edges and packs `neighbourLod - lod` as 4x4 bits. In `terrain.vert`,
an edge vertex snaps its along-edge coordinate down to a multiple of `2^delta`,
which lands exactly on a neighbour vertex (position *and* height, since the bake
is pure noise). The snapped vertices collapse into degenerate triangles that
close the T-junction — no skirts, no extra geometry.

## Drawing

One `DrawIndexedIndirect` per list, instance = one visible patch. There is **no
vertex buffer**: `terrain.vert` derives its position from `gl_VertexIndex` over
a shared 17x17 patch grid index buffer, fetches the height with an exact
`texelFetch` (height texels sit on grid vertices), and reads its atlas slot from
the patch entry. Three pipelines rasterize the same patches:

| Pipeline | Shader set | Where | Notes |
|----------|------------|-------|-------|
| Depth | `TerrainDepth` | `DepthPrePass` (phase 1 and 2) | depth + packed normal |
| Color (+ wireframe) | `Terrain` | `OpaquePass`, both lists | `LESS_OR_EQUAL`, depth writes off; `invariant gl_Position` makes it bit-identical to the prepass so early-z holds |
| Shadow | `TerrainShadow` (`terrain.vert` + `shadow.frag`) | `ShadowPass`, per cascade | depth-only, cull none, shares the mesh depth bias |

Backface culling is off everywhere — a heightfield has no closed backside.

## Shading

`terrain.frag` shades with the same `lighting.glsl` functions as `lit.frag`
(`DirectLighting` per tile light, `AmbientLighting` split-sum IBL, one
exposure), so it binds the geometry pass's inputs too: the GI indices and
bindless IBL maps (set 2), the light list, the tile light visibility (tile
counts ride in `TerrainParams.viewport.zw`), the screen-space AO map, the
cascades and the far mask. Terrain is a dielectric (metallic 0); roughness and
the baked AO come from the normal atlas. The baked AO multiplies the
screen-space AO on the ambient diffuse and feeds `SpecularOcclusion` on the
IBL specular.

**Materials.** The splat atlas (`R8_UINT`, nearest) holds one material ID
per texel; the material palette (`TerrainMaterialPaletteGPU`, rebuilt per
frame from `TerrainMaterialSet` so the GUI edits tiling live) maps an ID to
three **bindless detail map indices** (albedo, tangent-space normal +
smoothness, height) plus `1 / tileSize` and a rotation. `TerrainDetailUV`
(world XZ -> rotated, tiled uv), `TerrainSplatTexel` (nearest) and
`TerrainSplatBilinear` (the four surrounding texels + weights) live in
`terrainCommon.glsl`.

**Near shading.** Within `TerrainDetailSettings::nearDistance` the fragment
reads the four splat texels around it (`ShadeNear`): duplicate IDs merge
their bilinear weights, each distinct material samples its three detail maps
once at the world uv (`textureLod` with the mip derived from the world
position's gradients, so the per-material branches stay valid; explicit
gradient sampling cost almost twice as much), and the taps are
**height-blended** - each tap's relief is lifted by its bilinear weight, and
only taps within `heightBlendDepth` of the highest keep a share, so a
material's bumps cut through its neighbour instead of cross-fading. The
blended tangent-space normal is rotated back through each material's detail
rotation and reoriented onto the baked macro normal (RNM); smoothness becomes
roughness. Past `nearDistance` the result fades over `fadeWidth` into the
atlas albedo / normal / roughness, which the detail maps average to by
construction, so the transition carries no color shift. Beyond the fade the
shader skips the detail path entirely.

**Anti-tiling.** A 3-6 m tile repeats visibly at walking distance, so the
near path samples each map through **hex tiling** (Mikkelsen 2022,
`HexTiling`): a triangle grid over the tile uv gives every hex cell its own
hashed rotation and offset, the three cells around the pixel are sampled and
blended with sharpened barycentric weights (one cell dominates, edges blend),
and the tangent normals are rotated back out of their cell. Three samples per
map; `hexTiling` toggles it and `hexCellsPerTile` sets the cell size
(`TerrainParams.hexTiling`). It is per pixel, so it fades with the near blend.

Shadows come from two sources. Inside the CSM range the cascades draw the
per-cascade patch lists with terrain's own depth bias (`TerrainShadowSettings`).
Past the transition `ShadowVisibility` blends into
the SDF shadow mask, which `sdfShadow.comp` extends with a heightfield march:
`TerrainSampleHeight` (`terrainCommon.glsl`) resolves the finest resident node
under a world XZ through the quadtree index mips and samples the height atlas.
The ray starts on the sampled heightfield (`farBias` above it) rather than the
depth-reconstructed position, steps grow geometrically to `farMaxDistance`, and
the clearance over the terrain widens the penumbra (`farSoftness`).
Terrain and meshes both receive it; the mask exists only while the SDF volume
does.

## Verification

- ImGui **"Terrain"** panel (under Renderer Passes): requested / resident /
  pending / starved node counts, tiles uploaded and evicted this frame, free
  atlas slots, and the GPU visible-patch count (read back with a 2-frame delay).
- **Freeze streaming** pins residency so you can fly out and inspect the covering
  set and the LOD ring from outside.
- **Wireframe** shows patch density and T-junctions; **Debug mode** switches
  between Lit / LOD tint / Normals (near: with the detail normal) / UV grid /
  Roughness / Baked AO / Material ID (nearest splat texel) / Albedo (the
  blended albedo the lighting sees, unlit) / Blend weights (material tints
  mixed by the near blend weights) / Near/far (white = full detail).
- **Materials** tree: the near blend (near distance, fade width, height blend
  depth, detail normal strength, hex tiling on/off and cell size), then
  per-material tile size and rotation
  (live) and the bindless indices the palette carries.
- **Shadow** tree: cascade depth bias and the far heightfield march (steps 0
  turns it off for A/B, max distance, softness, bias). The SDF Shadow panel
  previews the mask itself.
- **F11** writes the presented frame to `screenshot_<frame>.png` (or launch with
  `ANGGEUM_SCREENSHOT=path.png@frame` for scripted runs); external screen capture
  of the swapchain comes back blank on this driver.
- **Draw culled patches** bypasses the cull and tints what would have been
  dropped (red = frustum, blue = occluded). With it on, the fragment shader also
  writes a pick value, and the panel decodes the patch under the mouse: LOD,
  node, patch, atlas slot, cull reason, world AABB, and the pass-1 cull thread
  ids (workgroup / local / global) for shader-side debugging. The pick readback
  buffers are created on first use, so the default path allocates nothing.
- CPU-side invariants (square root grid, patch/quad divisibility, 16-bit index
  space, `patchesPerNodeEdge == 8` matching the shader) are asserted at
  construction; delete `Assets/Cache/terrain_bake.tbake` to force a re-bake.
