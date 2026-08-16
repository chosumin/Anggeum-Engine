// The sentinels mirror TERRAIN_NODE_EMPTY / TERRAIN_NODE_INVALID in TerrainConfig.h.
const uint TERRAIN_NODE_EMPTY = 0xffffu;
const uint TERRAIN_NODE_INVALID = 0xfffeu; // >= this: nothing resident

// A node splits into 8x8 patches, the culling/draw granularity. Mirrors
// TerrainConfig::patchesPerNodeEdge (asserted CPU-side).
const uint TERRAIN_PATCHES_PER_EDGE = 8u;

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

float TerrainNodeSizeAt(float rootNodeSize, uint lodCount, uint lod)
{
    return rootNodeSize / float(1u << (lodCount - 1u - lod));
}

bool TerrainNodeResident(usampler2D quadTreeIndex, uint lod, uvec2 coord)
{
    return texelFetch(quadTreeIndex, ivec2(coord), int(lod)).r
        < TERRAIN_NODE_INVALID;
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
