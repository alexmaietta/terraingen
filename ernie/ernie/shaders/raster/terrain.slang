// Shared terrain resources and the CDLOD grid vertex, used by the shadow and colour passes.
// Every pass binds the same push-descriptor set:
//   0 Uniforms, 1 heights, 2 LOD patches, 3 height gradients, 4 clamping linear sampler,
//   5 Shadows, 6 shadow map, 7 shadow comparison sampler,
//   8 Surface, 9 albedo map, 10 detail albedo, 11 detail normal, 12 repeating detail sampler.

public struct Uniforms {
    public float4x4 viewProj;
    // xyz: unit direction towards the sun, w: ambient light fraction.
    public float4 sun;
    // Origin x, origin y (top edge), pixel size, vertical exaggeration.
    public float4 grid;
    // Min height, max height.
    public float4 heightRange;
    // Width, height, colour mode, wireframe.
    public uint4 dims;
    // xyz: camera forward, w: dot(eye, forward); view depth = dot(world, forward) - w.
    public float4 forward;
    // xyz: eye position that drives LOD morphing, w: unused.
    public float4 lodEye;
    // x: grid quads along a patch side, y: tint patches by LOD level.
    public uint4 lodInfo;
    // Morph start distance and 1 / (morph end - morph start) per LOD level, two levels per float4.
    public float4 morph[8];
    // xyz: camera position, w: unused.
    public float4 eye;
}

// Per draw: the light's view-projection for shadow passes, and the first patch of the draw.
public struct DrawConstants {
    public float4x4 lightViewProj;
    public uint patchOffset;
}

[[vk::binding(0, 0)]] public ConstantBuffer<Uniforms> u;
[[vk::binding(1, 0)]] public Texture2D<float> heights;
// Per patch: origin column, origin row, LOD level (vertex spacing 2^level cells), unused.
[[vk::binding(2, 0)]] public StructuredBuffer<uint4> patches;
// Central-difference height gradient (dz/dx east, dz/dy north) per raster cell, before
// exaggeration; filterable, unlike `heights`.
[[vk::binding(3, 0)]] public Texture2D<float2> gradients;
[[vk::binding(4, 0)]] public SamplerState clampSampler;
[[vk::push_constant]] public ConstantBuffer<DrawConstants> draw;

public float heightAt(int2 cell) {
    let last = int2(u.dims.xy) - int2(1, 1);
    return heights.Load(int3(clamp(cell, int2(0, 0), last), 0));
}

float2 morphRange(uint level) {
    let pair = u.morph[level / 2];
    return level % 2 == 0 ? pair.xy : pair.zw;
}

public float3 worldAt(float2 cell, float h) {
    return float3(u.grid.x + (cell.x + 0.5) * u.grid.z, u.grid.y - (cell.y + 0.5) * u.grid.z,
                  h * u.grid.w);
}

public struct GridVertex {
    public float3 world;
    public float height;
    public uint corner;
    public uint level;
}

// One vertex of an LOD patch (`LodQuadtree`): two triangles per grid quad, six vertices per quad,
// no vertex or index buffers. CDLOD geomorphing (Strugar 2009): within the last part of its
// level's range each odd vertex slides onto its even neighbour at lower index, so at morph 1 the
// patch is exactly the next coarser level's mesh. The quad diagonal runs from (1, 0) to (0, 1);
// collapsing towards lower indices keeps it on the coarse quad's diagonal. Odd vertices move rather
// than interpolate the coarse edge, and heights blend between the two endpoints, so `heights` needs
// no filtering. Cells past the raster clamp to its last row and column, collapsing their quads.
public GridVertex gridVertex(uint vertexIndex, uint instanceIndex) {
    let corners = uint2[6](uint2(0, 0), uint2(1, 0), uint2(0, 1), uint2(1, 0), uint2(1, 1),
                           uint2(0, 1));
    let tile = patches[draw.patchOffset + instanceIndex];
    let cells = u.lodInfo.x;
    let quad = vertexIndex / 6;
    let corner = vertexIndex % 6;
    let local = uint2(quad % cells, quad / cells) + corners[corner];
    let spacing = 1u << tile.z;
    let last = int2(u.dims.xy) - int2(1, 1);
    let unclamped = int2(tile.xy + local * spacing);
    let odd = int2(local & uint2(1, 1));
    let cell = min(unclamped, last);
    let coarse = min(unclamped - odd * int(spacing), last);

    let h = heightAt(cell);
    let range = morphRange(tile.z);
    let k = clamp((distance(u.lodEye.xyz, worldAt(float2(cell), h)) - range.x) * range.y, 0.0, 1.0);
    let morphed = lerp(h, heightAt(coarse), k);

    GridVertex v;
    v.world = worldAt(lerp(float2(cell), float2(coarse), k), morphed);
    v.height = morphed;
    v.corner = corner;
    v.level = tile.z;
    return v;
}

// Exaggerated terrain slope (dz/dx, dz/dy) from the full-resolution gradient at a world position
// rather than from the mesh, so shading does not change as LOD levels morph.
public float2 terrainSlope(float3 world) {
    let extent = float2(u.dims.xy) * u.grid.z;
    let uv = float2(world.x - u.grid.x, u.grid.y - world.y) / extent;
    return gradients.SampleLevel(clampSampler, uv, 0) * u.grid.w;
}

public float3 slopeNormal(float2 slope) {
    return normalize(float3(-slope.x, -slope.y, 1.0));
}
