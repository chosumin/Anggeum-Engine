// The near material blend: the detail maps of the materials around a point,
// height-weighted into one albedo / normal / roughness. Shared by the
// per-pixel path in terrain.frag and the page render that fills the shading
// cache. The includer declares, before this file: `params` (TerrainParams),
// `palette` (materials[] + count), `splatAtlas` (usampler2D) and
// `bindlessTextures2D[]`.

// One tint per palette slot; sized by the palette so a bigger palette
// fails to compile until this grows with it.
const vec3 MATERIAL_COLORS[TERRAIN_MAX_MATERIALS] = vec3[](
    vec3(0.2, 0.8, 0.2), vec3(0.6, 0.4, 0.2), vec3(0.5, 0.5, 0.5), vec3(1.0, 1.0, 1.0),
    vec3(1.0, 0.2, 0.2), vec3(0.2, 0.2, 1.0), vec3(1.0, 1.0, 0.2), vec3(1.0, 0.2, 1.0),
    vec3(0.2, 1.0, 1.0), vec3(1.0, 0.6, 0.0), vec3(0.5, 0.0, 0.8), vec3(0.0, 0.5, 0.3),
    vec3(0.8, 0.5, 0.5), vec3(0.3, 0.3, 0.0), vec3(0.6, 0.8, 1.0), vec3(0.1, 0.1, 0.1));

vec3 MaterialTint(uint materialId)
{
    return MATERIAL_COLORS[min(materialId, TERRAIN_MAX_MATERIALS - 1u)];
}

struct NearSurface
{
    vec3 albedo;
    vec3 normal;    // world axes, unperturbed = +Y
    float roughness;
    vec3 idTint;    // MaterialTint blended by the same weights (debug)
};

// LOD from the world-position gradients: the mip textureGrad would pick, but
// textureLod runs at full sampler rate and textureGrad does not.
float DetailLod(uint textureIndex, vec2 duvdx, vec2 duvdy)
{
    float size = float(textureSize(bindlessTextures2D[nonuniformEXT(textureIndex)], 0).x);
    return 0.5 * log2(max(dot(duvdx, duvdx), dot(duvdy, duvdy)) * size * size);
}

vec4 SampleDetail(uint textureIndex, vec2 uv, float lod)
{
    return textureLod(bindlessTextures2D[nonuniformEXT(textureIndex)], uv, lod);
}

// One splat tap: its material's detail maps at the world uv
struct DetailTap
{
    vec3 albedo;
    vec3 normal;   // tangent space of the material's detail uv, decoded
    float smoothness;
    vec2 rotation; // cos, sin of the material's detail rotation
    float h;
};

// Hex tiling (Mikkelsen 2022): a triangle grid over the tile uv; every cell
// gets its own rotation and offset, and the three cells around a point blend
// with sharpened barycentric weights, so no two spots share a tile.
struct HexTap
{
    vec2 uv;
    vec2 rotation; // cos, sin of the cell's rotation
    float w;
};

HexTap MakeHexTap(vec2 uv, ivec2 cell, float w, float cellsPerTile)
{
    const mat2 unskew = mat2(1.0, 0.0, 0.5, 0.8660254);
    uint h = TerrainHashUint(uvec2(cell + 0x4000));
    float angle = float(h & 0xffffu) * (6.2831853 / 65535.0);
    HexTap t;
    t.rotation = vec2(cos(angle), sin(angle));
    vec2 center = unskew * vec2(cell) / cellsPerTile;
    vec2 d = uv - center;
    t.uv = center + vec2(d.x * t.rotation.x - d.y * t.rotation.y,
        d.x * t.rotation.y + d.y * t.rotation.x)
        + vec2(float((h >> 16u) & 0xffu), float(h >> 24u)) / 255.0;
    t.w = w;
    return t;
}

void HexTiling(vec2 uv, float cellsPerTile, out HexTap taps[3])
{
    const mat2 skew = mat2(1.0, 0.0, -0.57735027, 1.15470054);
    const float SHARPNESS = 7.0; // keeps mostly one cell per pixel, blending only at edges
    vec2 skewed = skew * (uv * cellsPerTile);
    ivec2 base = ivec2(floor(skewed));
    vec2 f = skewed - vec2(base);
    float z = 1.0 - f.x - f.y;
    vec3 w;
    ivec2 c0, c1, c2;
    if (z > 0.0)
    {
        w = vec3(z, f.y, f.x);
        c0 = base; c1 = base + ivec2(0, 1); c2 = base + ivec2(1, 0);
    }
    else
    {
        w = vec3(-z, 1.0 - f.y, 1.0 - f.x);
        c0 = base + ivec2(1, 1); c1 = base + ivec2(1, 0); c2 = base + ivec2(0, 1);
    }
    w = pow(w, vec3(SHARPNESS));
    w /= w.x + w.y + w.z;
    taps[0] = MakeHexTap(uv, c0, w.x, cellsPerTile);
    taps[1] = MakeHexTap(uv, c1, w.y, cellsPerTile);
    taps[2] = MakeHexTap(uv, c2, w.z, cellsPerTile);
}

// w > 0: merged-away taps are skipped by the caller.
DetailTap SampleTap(uint id, float w, vec2 worldXZ, vec2 dWdx, vec2 dWdy)
{
    TerrainMaterial material = palette.materials[min(id, palette.count.x - 1u)];
    vec2 uv = TerrainDetailUV(material, worldXZ);
    vec2 duvdx = TerrainDetailUV(material, dWdx); // linear, so deltas map alike
    vec2 duvdy = TerrainDetailUV(material, dWdy);
    float lod = DetailLod(material.textures.x, duvdx, duvdy); // the three maps share a size

    HexTap hex[3];
    int tapCount = 1;
    hex[0].uv = uv;
    hex[0].rotation = vec2(1.0, 0.0);
    hex[0].w = 1.0;
    if (params.hexTiling.x != 0.0)
    {
        HexTiling(uv, params.hexTiling.y, hex);
        tapCount = 3;
    }

    // A lone material (w = 1 after the merge) wins the height blend outright.
    bool needHeight = w < 0.999;
    DetailTap tap;
    tap.albedo = vec3(0.0);
    tap.normal = vec3(0.0);
    tap.smoothness = 0.0;
    float height = 0.0;
    for (int i = 0; i < tapCount; ++i)
    {
        HexTap h = hex[i];
        tap.albedo += h.w * SampleDetail(material.textures.x, h.uv, lod).rgb;
        vec4 ns = SampleDetail(material.textures.y, h.uv, lod);
        vec3 n = ns.xyz * 2.0 - 1.0;
        // Undo the cell's rotation so the tilt is in the material's uv frame.
        tap.normal += h.w * vec3(n.x * h.rotation.x + n.y * h.rotation.y,
            -n.x * h.rotation.y + n.y * h.rotation.x, n.z);
        tap.smoothness += h.w * ns.a;
        if (needHeight)
            height += h.w * SampleDetail(material.textures.z, h.uv, lod).r;
    }
    tap.rotation = material.tiling.yz;
    tap.h = height + w;
    return tap;
}

void AccumulateTap(inout NearSurface s, inout float sum, DetailTap tap, uint id, float hMax)
{
    float b = max(tap.h - hMax + params.detailBlend.z, 0.0);
    // Tangent-space xy back through the material rotation: +u = (cos, -sin), +v = (sin, cos) in world XZ.
    vec3 n = tap.normal;
    vec2 tilt = n.x * vec2(tap.rotation.x, -tap.rotation.y) + n.y * vec2(tap.rotation.y, tap.rotation.x);
    s.albedo += b * tap.albedo;
    s.normal += b * vec3(tilt.x, n.z, tilt.y);
    s.roughness += b * (1.0 - tap.smoothness);
    s.idTint += b * MaterialTint(id);
    sum += b;
}

// Four surrounding splat texels of the node (slot, tileUV) -> up to four
// materials, duplicates merged so each is sampled once. dWdx/dWdy are the
// world XZ footprint of the shaded point (screen pixel or cache texel).
NearSurface ShadeNear(uint slot, vec2 tileUV, vec2 worldXZ, vec2 dWdx, vec2 dWdy)
{
    ivec2 base;
    vec2 f;
    TerrainSplatBilinear(slot, tileUV, params, base, f);

    // One gather at the shared corner returns the 2x2 as (0,1) (1,1) (1,0) (0,0).
    uvec4 gathered = textureGather(splatAtlas,
        (vec2(base) + 1.0) / vec2(textureSize(splatAtlas, 0)), 0) & TERRAIN_SPLAT_ID_MASK;
    uint ids[4];
    float w[4];
    ids[0] = gathered.w; ids[1] = gathered.z; ids[2] = gathered.x; ids[3] = gathered.y;
    w[0] = (1.0 - f.x) * (1.0 - f.y);
    w[1] = f.x * (1.0 - f.y);
    w[2] = (1.0 - f.x) * f.y;
    w[3] = f.x * f.y;

    // Merge each tap into the first earlier tap with the same ID.
    for (int i = 1; i < 4; ++i)
        for (int j = 0; j < i; ++j)
            if (ids[i] == ids[j])
            {
                w[j] += w[i];
                w[i] = 0.0;
                break;
            }

    DetailTap taps[4];
    float hMax = 0.0; // every sampled tap has h = relief + w > 0
    for (int i = 0; i < 4; ++i)
    {
        if (w[i] <= 0.0)
            continue;

        taps[i] = SampleTap(ids[i], w[i], worldXZ, dWdx, dWdy);
        hMax = max(hMax, taps[i].h);
    }

    // Height blend: the bilinear weight lifts each tap's relief, and only
    // taps within detailBlend.z of the highest keep a share.
    NearSurface s;
    s.albedo = vec3(0.0);
    s.normal = vec3(0.0);
    s.roughness = 0.0;
    s.idTint = vec3(0.0);
    float sum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        if (w[i] <= 0.0)
            continue;
        AccumulateTap(s, sum, taps[i], ids[i], hMax);
    }

    float invSum = 1.0 / max(sum, 1e-4);
    s.albedo *= invSum;
    s.roughness *= invSum;
    s.idTint *= invSum;
    s.normal = normalize(vec3(s.normal.x * params.detailBlend.w, s.normal.y,
        s.normal.z * params.detailBlend.w));

    return s;
}

// Reoriented normal mapping, with +Y as the shared up axis.
vec3 PerturbNormal(vec3 N, vec3 detail)
{
    vec3 t = N.xzy + vec3(0.0, 0.0, 1.0);
    vec3 u = detail.xzy * vec3(-1.0, -1.0, 1.0);
    vec3 r = t * dot(t, u) / t.z - u;
    return normalize(r.xzy);
}
