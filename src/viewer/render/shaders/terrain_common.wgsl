struct Uniforms {
    view_proj: mat4x4<f32>,
    // xyz: unit direction towards the sun, w: ambient light fraction.
    sun: vec4<f32>,
    // origin x, origin y (top edge), pixel size, vertical exaggeration.
    grid: vec4<f32>,
    // min height, max height.
    height_range: vec4<f32>,
    // width, height, colour mode, wireframe.
    dims: vec4<u32>,
    // xyz: camera forward, w: dot(eye, forward); view depth = dot(world, forward) - w.
    forward: vec4<f32>,
    // xyz: eye position that drives LOD morphing, w: unused.
    lod_eye: vec4<f32>,
    // x: grid quads along a patch side, y: tint patches by LOD level.
    lod_info: vec4<u32>,
    // Morph start distance and 1 / (morph end - morph start) per LOD level, two levels per vec4.
    morph: array<vec4<f32>, 8>,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var heights: texture_2d<f32>;
// Per instance: origin column, origin row, LOD level (vertex spacing 2^level cells), unused.
@group(0) @binding(2) var<storage, read> patches: array<vec4<u32>>;

fn height_at(cell: vec2<i32>) -> f32 {
    let last = vec2<i32>(u.dims.xy) - vec2<i32>(1, 1);
    return textureLoad(heights, clamp(cell, vec2<i32>(0, 0), last), 0).r;
}

fn morph_range(level: u32) -> vec2<f32> {
    let pair = u.morph[level / 2u];
    if (level % 2u == 0u) {
        return pair.xy;
    }
    return pair.zw;
}

fn world_at(cell: vec2<f32>, h: f32) -> vec3<f32> {
    return vec3<f32>(u.grid.x + (cell.x + 0.5) * u.grid.z, u.grid.y - (cell.y + 0.5) * u.grid.z,
                     h * u.grid.w);
}

struct GridVertex {
    world: vec3<f32>,
    height: f32,
    corner: u32,
    level: u32,
}

// One vertex of an LOD patch (`LodQuadtree`): two triangles per grid quad, six vertices per quad,
// no vertex or index buffers. CDLOD geomorphing (Strugar 2009): within the last part of
// its level's range each odd vertex slides onto its even neighbour at lower index, so at morph 1 the
// patch is exactly the next coarser level's mesh. The quad diagonal runs from (1, 0) to (0, 1);
// collapsing towards lower indices keeps it on the coarse quad's diagonal. Odd vertices move rather
// than interpolate the coarse edge, and heights blend between the two endpoints, so `heights` needs
// no filtering. Cells past the raster clamp to its last row and column, collapsing their quads.
fn grid_vertex(vertex_index: u32, instance_index: u32) -> GridVertex {
    var corners = array<vec2<u32>, 6>(
        vec2<u32>(0u, 0u), vec2<u32>(1u, 0u), vec2<u32>(0u, 1u),
        vec2<u32>(1u, 0u), vec2<u32>(1u, 1u), vec2<u32>(0u, 1u));

    let tile = patches[instance_index];
    let cells = u.lod_info.x;
    let quad = vertex_index / 6u;
    let corner = vertex_index % 6u;
    let local = vec2<u32>(quad % cells, quad / cells) + corners[corner];
    let spacing = 1u << tile.z;
    let last = vec2<i32>(u.dims.xy) - vec2<i32>(1, 1);
    let unclamped = vec2<i32>(tile.xy + local * spacing);
    let odd = vec2<i32>(local & vec2<u32>(1u, 1u));
    let cell = min(unclamped, last);
    let coarse = min(unclamped - odd * i32(spacing), last);

    let h = height_at(cell);
    let range = morph_range(tile.z);
    let k = clamp((distance(u.lod_eye.xyz, world_at(vec2<f32>(cell), h)) - range.x) * range.y,
                  0.0, 1.0);
    let morphed = mix(h, height_at(coarse), k);

    var v: GridVertex;
    v.world = world_at(mix(vec2<f32>(cell), vec2<f32>(coarse), k), morphed);
    v.height = morphed;
    v.corner = corner;
    v.level = tile.z;
    return v;
}
