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
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var heights: texture_2d<f32>;

fn height_at(cell: vec2<i32>) -> f32 {
    let last = vec2<i32>(u.dims.xy) - vec2<i32>(1, 1);
    return textureLoad(heights, clamp(cell, vec2<i32>(0, 0), last), 0).r;
}

struct GridVertex {
    world: vec3<f32>,
    normal: vec3<f32>,
    height: f32,
    corner: u32,
}

// Two triangles per grid quad, six vertices per quad, no vertex or index buffers.
fn grid_vertex(vertex_index: u32) -> GridVertex {
    var corners = array<vec2<u32>, 6>(
        vec2<u32>(0u, 0u), vec2<u32>(1u, 0u), vec2<u32>(0u, 1u),
        vec2<u32>(1u, 0u), vec2<u32>(1u, 1u), vec2<u32>(0u, 1u));

    let quad = vertex_index / 6u;
    let corner = vertex_index % 6u;
    let quads_per_row = u.dims.x - 1u;
    let cell = vec2<u32>(quad % quads_per_row, quad / quads_per_row) + corners[corner];
    let c = vec2<i32>(cell);

    let pixel = u.grid.z;
    let exaggeration = u.grid.w;
    let h = height_at(c);

    // Central differences; rows run southwards, so the north neighbour is row - 1.
    let dzdx = (height_at(c + vec2<i32>(1, 0)) - height_at(c - vec2<i32>(1, 0))) * exaggeration /
               (2.0 * pixel);
    let dzdy = (height_at(c - vec2<i32>(0, 1)) - height_at(c + vec2<i32>(0, 1))) * exaggeration /
               (2.0 * pixel);

    var v: GridVertex;
    v.world = vec3<f32>(u.grid.x + (f32(cell.x) + 0.5) * pixel,
                        u.grid.y - (f32(cell.y) + 0.5) * pixel,
                        h * exaggeration);
    v.normal = normalize(vec3<f32>(-dzdx, -dzdy, 1.0));
    v.height = h;
    v.corner = corner;
    return v;
}
