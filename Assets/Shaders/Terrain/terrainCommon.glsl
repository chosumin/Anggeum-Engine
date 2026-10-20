// The sentinels mirror TERRAIN_NODE_EMPTY / TERRAIN_NODE_INVALID in TerrainConfig.h.
const uint TERRAIN_NODE_EMPTY = 0xffffu;
const uint TERRAIN_NODE_INVALID = 0xfffeu; // >= this: nothing resident

// A node splits into 8x8 patches, the culling/draw granularity. Mirrors
// TerrainConfig::patchesPerNodeEdge (asserted CPU-side).
const uint TERRAIN_PATCHES_PER_EDGE = 8u;

// Quads per node edge; mirrors TerrainConfig::quadCountPerNodeEdge.
const uint TERRAIN_NODE_QUADS = 128u;

// Quads per patch edge (16) - the draw instance is one patch.
const uint TERRAIN_PATCH_QUADS = TERRAIN_NODE_QUADS / TERRAIN_PATCHES_PER_EDGE;

// Patch list entry .z: patch index in bits 0-7; debug mode keeps culled
// patches and tags why (mirrors TerrainConfig.h).
const uint TERRAIN_CULL_FRUSTUM = 0x100u;
const uint TERRAIN_CULL_OCCLUDED = 0x200u;

// Node list entry coord packing: [ lod:4 ][ y:14 ][ x:14 ].
uint TerrainPackCoord(uint lod, uvec2 coord)
{
    return (lod << 28) | (coord.y << 14) | coord.x;
}

uvec2 TerrainUnpackCoord(uint packedCoord)
{
    return uvec2(packedCoord & 0x3fffu, (packedCoord >> 14) & 0x3fffu);
}

uint TerrainUnpackLod(uint packedCoord)
{
    return packedCoord >> 28;
}

struct TerrainParams
{
    vec4 heightMinMaxInvAtlas; // x = min, y = max, zw = 1 / heightAtlasExtent
    vec4 invColorAtlasBorder;  // xy = 1 / colorAtlasExtent, z = borderTexels
    ivec4 debugMode;           // x: 0 lit, 1 LOD tint, 2 normals, 3 uv grid, 4 roughness, 5 baked AO, 6 material ID, 7 albedo, 8 blend weights, 9 near/far
    vec4 worldParams;          // xy = worldOrigin, z = rootNodeSize, w = lodCount
    vec4 atlasInfo;            // x = slotsPerRow, y = heightTexels, z = colorTexels, w = splatTexels
    vec4 viewport;             // xy = size, zw = light tile counts
    vec4 detailBlend;          // x = near distance, y = fade width, z = height blend depth, w = detail normal strength
    vec4 hexTiling;            // x = on, y = hex cells per detail tile
};

uint TerrainHashUint(uvec2 p)
{
    uint h = p.x * 0x8da6b343u ^ p.y * 0xd8163841u;
    h ^= h >> 15u; h *= 0x2c1b3c6du; h ^= h >> 12u;
    return h;
}

// Mirrors TerrainMaterialGPU: bindless detail map indices and world tiling.
struct TerrainMaterial
{
    uvec4 textures; // x albedo, y normal + smoothness, z height
    vec4 tiling;    // x = 1 / tileSize, y = cos(rotation), z = sin(rotation)
};
const uint TERRAIN_MAX_MATERIALS = 16u;
const uint TERRAIN_SPLAT_ID_MASK = 0x7fu; // bit 7 reserved for the cliff flag

// World XZ -> detail UV: rotate, then scale to tiles.
vec2 TerrainDetailUV(TerrainMaterial material, vec2 worldXZ)
{
    vec2 p = worldXZ * material.tiling.x;
    return vec2(p.x * material.tiling.y - p.y * material.tiling.z,
                p.x * material.tiling.z + p.y * material.tiling.y);
}

// Splat atlas texel nearest to a node-relative uv: splat texel k sits on
// grid vertex k * splatQuadsPerTexel, so k = uv * (splatTexels - 1).
ivec2 TerrainSplatTexel(uint slot, vec2 tileUV, TerrainParams params)
{
    uint slotsPerRow = uint(params.atlasInfo.x);
    uint splatTexels = uint(params.atlasInfo.w);
    uvec2 origin = uvec2(slot % slotsPerRow, slot / slotsPerRow) * splatTexels;
    return ivec2(origin + uvec2(round(tileUV * float(splatTexels - 1u))));
}

// The four splat texels around a node-relative uv: base = the min corner,
// frac = the bilinear weight toward the +x / +y neighbours (all in one slot).
void TerrainSplatBilinear(uint slot, vec2 tileUV, TerrainParams params,
    out ivec2 base, out vec2 frac)
{
    uint slotsPerRow = uint(params.atlasInfo.x);
    uint splatTexels = uint(params.atlasInfo.w);
    uvec2 origin = uvec2(slot % slotsPerRow, slot / slotsPerRow) * splatTexels;
    vec2 coord = tileUV * float(splatTexels - 1u);
    vec2 baseF = min(floor(coord), float(splatTexels - 2u));
    base = ivec2(origin) + ivec2(baseF);
    frac = coord - baseF;
}

// Normal atlas texel: rg = normal xz * 0.5 + 0.5, b = roughness, a = horizon AO. 
// A heightfield normal always points up, so y is reconstructed.
vec3 TerrainDecodeNormal(vec4 packedNormal)
{
    vec2 xz = packedNormal.rg * 2.0 - 1.0;
    return normalize(vec3(xz.x, sqrt(max(1.0 - dot(xz, xz), 0.0)), xz.y));
}

struct TerrainTraversalPush
{
    vec2 cameraXZ;
    vec2 worldOrigin;
    float rootNodeSize;
    float ringRadiusScale;
    uint lodCount;
    uint rootTiles;
    uint patchIndexCount;
};

float TerrainNodeSizeAt(float rootNodeSize, uint lodCount, uint lod)
{
    return rootNodeSize / float(1u << (lodCount - 1u - lod));
}

// World AABB of one patch: XZ from its node and index, Y from the node's
// baked min/max (heightBounds: x = min, y = max, z = conservative pad).
void TerrainPatchAABB(uint packedCoord, uint patchIdx, uint minMaxHeight, vec4 heightBounds,
    vec2 worldOrigin, float rootNodeSize, uint lodCount, out vec3 aabbMin, out vec3 aabbMax)
{
    uint lod = TerrainUnpackLod(packedCoord);
    uvec2 nodeCoord = TerrainUnpackCoord(packedCoord);
    uvec2 patchXY = uvec2(patchIdx % TERRAIN_PATCHES_PER_EDGE, patchIdx / TERRAIN_PATCHES_PER_EDGE);

    float nodeSize = TerrainNodeSizeAt(rootNodeSize, lodCount, lod);
    float patchSize = nodeSize / float(TERRAIN_PATCHES_PER_EDGE);
    vec2 patchMin = worldOrigin + vec2(nodeCoord) * nodeSize + vec2(patchXY) * patchSize;

    float minHeight = mix(heightBounds.x, heightBounds.y,
        float(minMaxHeight & 0xffffu) / 65535.0) - heightBounds.z;
    float maxHeight = mix(heightBounds.x, heightBounds.y,
        float(minMaxHeight >> 16) / 65535.0) + heightBounds.z;

    aabbMin = vec3(patchMin.x, minHeight, patchMin.y);
    aabbMax = vec3(patchMin.x + patchSize, maxHeight, patchMin.y + patchSize);
}

bool TerrainNodeResident(usampler2D quadTreeIndex, uint lod, uvec2 coord)
{
    return texelFetch(quadTreeIndex, ivec2(coord), int(lod)).r
        < TERRAIN_NODE_INVALID;
}

// Terrain height under worldXZ from the finest resident node covering it -
// the node the traversal draws there, so the result matches the rendered
// surface up to bilinear vs. triangle interpolation. False outside the
// world or where nothing is resident (any stage; explicit LOD 0 fetch).
bool TerrainSampleHeight(usampler2D quadTreeIndex, sampler2D heightAtlas,
    TerrainParams params, vec2 worldXZ, out float height)
{
    uint lodCount = uint(params.worldParams.w);
    float sectorSize = TerrainNodeSizeAt(params.worldParams.z, lodCount, 0u);
    vec2 sectorPos = (worldXZ - params.worldParams.xy) / sectorSize;
    ivec2 sectorsPerSide = textureSize(quadTreeIndex, 0);
    if (any(lessThan(sectorPos, vec2(0.0)))
        || any(greaterThanEqual(sectorPos, vec2(sectorsPerSide))))
        return false;

    ivec2 sector = ivec2(sectorPos);
    uint slot = TERRAIN_NODE_EMPTY;
    uint lod = 0u;
    for (; lod < lodCount; ++lod)
    {
        slot = texelFetch(quadTreeIndex, sector >> int(lod), int(lod)).r;
        if (slot < TERRAIN_NODE_INVALID)
            break;
    }
    if (slot >= TERRAIN_NODE_INVALID)
        return false;

    float nodeSize = TerrainNodeSizeAt(params.worldParams.z, lodCount, lod);
    vec2 nodeMin = params.worldParams.xy + vec2(sector >> int(lod)) * nodeSize;
    vec2 local = (worldXZ - nodeMin) / nodeSize; // 0..1 across the node

    uint slotsPerRow = uint(params.atlasInfo.x);
    vec2 slotOrigin = vec2(slot % slotsPerRow, slot / slotsPerRow) * params.atlasInfo.y;
    vec2 texel = slotOrigin + local * float(TERRAIN_NODE_QUADS) + 0.5;
    float height01 = textureLod(heightAtlas, texel * params.heightMinMaxInvAtlas.zw, 0.0).r;
    height = mix(params.heightMinMaxInvAtlas.x, params.heightMinMaxInvAtlas.y, height01);
    return true;
}

// The covering-set refinement predicate: refine into children when the node
// is inside the child ring AND all four children are resident. Shared so the
// node list traversal and the per-sector LOD map walk can never diverge.
bool TerrainShouldRefine(usampler2D quadTreeIndex, uint lod, uvec2 coord,
    vec2 cameraXZ, vec2 worldOrigin, float rootNodeSize, float ringRadiusScale,
    uint lodCount)
{
    if (lod == 0u)
        return false;

    float size = TerrainNodeSizeAt(rootNodeSize, lodCount, lod);
    vec2 nodeMin = worldOrigin + vec2(coord) * size;
    vec2 closest = clamp(cameraXZ, nodeMin, nodeMin + size);
    float childRadius = ringRadiusScale
        * TerrainNodeSizeAt(rootNodeSize, lodCount, lod - 1u);
    if (length(cameraXZ - closest) > childRadius)
        return false;

    uvec2 childBase = coord * 2u;
    return TerrainNodeResident(quadTreeIndex, lod - 1u, childBase)
        && TerrainNodeResident(quadTreeIndex, lod - 1u, childBase + uvec2(1u, 0u))
        && TerrainNodeResident(quadTreeIndex, lod - 1u, childBase + uvec2(0u, 1u))
        && TerrainNodeResident(quadTreeIndex, lod - 1u, childBase + uvec2(1u, 1u));
}
