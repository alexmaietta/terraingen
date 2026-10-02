// Terrain resources and the CDLOD grid vertex, used by the terrain's shadow and colour passes.
// Besides the frame (bindings 0 to 4, frame.slang) and material (5 to 9, surface.slang) the
// passes bind
//   10 TerrainUniforms, 11 heights, 12 LOD patches, 13 height gradients.
__exported import frame;

public struct TerrainUniforms {
    // Origin x, origin y (top edge), pixel size, vertical exaggeration.
    public float4 grid;
    // Min height, max height.
    public float4 heightRange;
    // Width, height, colour mode, tint patches by LOD level.
    public uint4 dims;
    // xyz: eye position that drives LOD morphing, w: unused.
    public float4 lodEye;
    // x: grid quads along a patch side.
    public uint4 lodInfo;
    // Morph start distance and 1 / (morph end - morph start) per LOD level, two levels per float4.
    public float4 morph[8];
    // Material, prim id.
    public uint4 ids;
}

[[vk::binding(10, 0)]] public ConstantBuffer<TerrainUniforms> t;
[[vk::binding(11, 0)]] public Texture2D<float> heights;
// Per patch: origin column, origin row, LOD level (vertex spacing 2^level cells), unused.
[[vk::binding(12, 0)]] public StructuredBuffer<uint4> patches;
// Central-difference height gradient (dz/dx east, dz/dy north) per raster cell, before
// exaggeration; filterable, unlike `heights`.
[[vk::binding(13, 0)]] public Texture2D<float2> gradients;

public float heightAt(int2 cell) {
    let last = int2(t.dims.xy) - int2(1, 1);
    return heights.Load(int3(clamp(cell, int2(0, 0), last), 0));
}

float2 morphRange(uint level) {
    let pair = t.morph[level / 2];
    return level % 2 == 0 ? pair.xy : pair.zw;
}

public float3 worldAt(float2 cell, float h) {
    return float3(t.grid.x + (cell.x + 0.5) * t.grid.z, t.grid.y - (cell.y + 0.5) * t.grid.z,
                  h * t.grid.w);
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
    let tile = patches[draw.first + instanceIndex];
    let cells = t.lodInfo.x;
    let quad = vertexIndex / 6;
    let corner = vertexIndex % 6;
    let local = uint2(quad % cells, quad / cells) + corners[corner];
    let spacing = 1u << tile.z;
    let last = int2(t.dims.xy) - int2(1, 1);
    let unclamped = int2(tile.xy + local * spacing);
    let odd = int2(local & uint2(1, 1));
    let cell = min(unclamped, last);
    let coarse = min(unclamped - odd * int(spacing), last);

    let h = heightAt(cell);
    let range = morphRange(tile.z);
    let k = clamp((distance(t.lodEye.xyz, worldAt(float2(cell), h)) - range.x) * range.y, 0.0, 1.0);
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
    let extent = float2(t.dims.xy) * t.grid.z;
    let uv = float2(world.x - t.grid.x, t.grid.y - world.y) / extent;
    return gradients.SampleLevel(clampSampler, uv, 0) * t.grid.w;
}

public float3 slopeNormal(float2 slope) {
    return normalize(float3(-slope.x, -slope.y, 1.0));
}
