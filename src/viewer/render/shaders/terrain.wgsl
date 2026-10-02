struct Shadows {
    light_view_proj: array<mat4x4<f32>, 4>,
    // Far view depth of each cascade.
    splits: vec4<f32>,
    // World size of one shadow map texel in each cascade.
    texel_world: vec4<f32>,
    // Filter radius (texels), normal offset (texels), cascade count, enabled.
    params: vec4<f32>,
    // x: tint cascades.
    debug: vec4<u32>,
    // 16 Poisson disk samples in the unit disk, two per vec4.
    poisson: array<vec4<f32>, 8>,
}

@group(1) @binding(0) var<uniform> s: Shadows;
@group(1) @binding(1) var shadow_map: texture_depth_2d_array;
@group(1) @binding(2) var shadow_sampler: sampler_comparison;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) world: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) height: f32,
    @location(3) barycentric: vec3<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var barycentric = array<vec3<f32>, 6>(
        vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0),
        vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0));
    let v = grid_vertex(vertex_index);
    var out: VertexOutput;
    out.position = u.view_proj * vec4<f32>(v.world, 1.0);
    out.world = v.world;
    out.normal = v.normal;
    out.height = v.height;
    out.barycentric = barycentric[v.corner];
    return out;
}

fn colormap(t: f32) -> vec3<f32> {
    var stops = array<vec3<f32>, 5>(
        vec3<f32>(0.16, 0.18, 0.30), vec3<f32>(0.20, 0.42, 0.52), vec3<f32>(0.56, 0.62, 0.44),
        vec3<f32>(0.80, 0.72, 0.52), vec3<f32>(0.96, 0.95, 0.92));
    let x = clamp(t, 0.0, 1.0) * 4.0;
    let i = u32(min(floor(x), 3.0));
    return mix(stops[i], stops[i + 1u], x - f32(i));
}

// Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare".
fn interleaved_gradient_noise(pixel: vec2<f32>) -> f32 {
    return fract(52.9829189 * fract(dot(pixel, vec2<f32>(0.06711056, 0.00583715))));
}

fn poisson_sample(i: u32) -> vec2<f32> {
    let pair = s.poisson[i / 2u];
    if (i % 2u == 0u) {
        return pair.xy;
    }
    return pair.zw;
}

fn cascade_index(view_depth: f32) -> u32 {
    let count = u32(s.params.z);
    for (var c = 0u; c < count; c = c + 1u) {
        if (view_depth < s.splits[c]) {
            return c;
        }
    }
    return count;
}

// Poisson-disk PCF over a cascaded shadow map. Each tap is a hardware 2x2 comparison; the disk is
// rotated per pixel so undersampling shows as fine noise rather than banding. Normal offset
// (Holbert 2011) pushes the lookup off the surface to avoid self-shadowing acne.
fn shadow_visibility(world: vec3<f32>, normal: vec3<f32>, n_dot_l: f32, view_depth: f32,
                     pixel: vec2<f32>) -> f32 {
    let cascade = cascade_index(view_depth);
    if (cascade >= u32(s.params.z)) {
        return 1.0;
    }
    let texel_world = s.texel_world[cascade];
    let offset = normal * s.params.y * texel_world * (1.0 - clamp(n_dot_l, 0.0, 1.0));
    let clip = s.light_view_proj[cascade] * vec4<f32>(world + offset, 1.0);
    let ndc = clip.xyz / clip.w;
    let uv = vec2<f32>(0.5 * ndc.x + 0.5, 0.5 - 0.5 * ndc.y);
    if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0)) || ndc.z > 1.0) {
        return 1.0;
    }

    let angle = 6.28318530718 * interleaved_gradient_noise(pixel);
    let rotation = mat2x2<f32>(cos(angle), sin(angle), -sin(angle), cos(angle));
    let radius = s.params.x / f32(textureDimensions(shadow_map).x);
    var lit = 0.0;
    for (var i = 0u; i < 16u; i = i + 1u) {
        let tap = uv + rotation * poisson_sample(i) * radius;
        lit = lit + textureSampleCompareLevel(shadow_map, shadow_sampler, tap, cascade, ndc.z);
    }
    return lit / 16.0;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let edge_width = fwidth(in.barycentric);

    let normal = normalize(in.normal);
    let n_dot_l = dot(normal, u.sun.xyz);
    var visibility = 1.0;
    if (s.params.w > 0.0) {
        if (n_dot_l <= 0.0) {
            visibility = 0.0;
        } else {
            let view_depth = dot(in.world, u.forward.xyz) - u.forward.w;
            visibility = shadow_visibility(in.world, normal, n_dot_l, view_depth,
                                           in.position.xy);
        }
    }
    let diffuse = max(n_dot_l, 0.0) * visibility;
    let shade = u.sun.w + (1.0 - u.sun.w) * diffuse;
    let range = max(u.height_range.y - u.height_range.x, 1e-6);
    let t = (in.height - u.height_range.x) / range;

    var color = vec3<f32>(shade);
    if (u.dims.z == 1u) {
        color = colormap(t);
    } else if (u.dims.z == 2u) {
        color = colormap(t) * shade;
    }

    if (s.debug.x == 1u && s.params.w > 0.0) {
        var tints = array<vec3<f32>, 5>(
            vec3<f32>(1.0, 0.55, 0.55), vec3<f32>(0.55, 1.0, 0.55), vec3<f32>(0.55, 0.65, 1.0),
            vec3<f32>(1.0, 1.0, 0.5), vec3<f32>(1.0, 1.0, 1.0));
        let view_depth = dot(in.world, u.forward.xyz) - u.forward.w;
        color = color * tints[cascade_index(view_depth)];
    }

    if (u.dims.w == 1u) {
        let edge = smoothstep(vec3<f32>(0.0), 1.5 * edge_width, in.barycentric);
        let line = 1.0 - min(edge.x, min(edge.y, edge.z));
        color = mix(color, vec3<f32>(0.05, 0.05, 0.06), line);
    }
    return vec4<f32>(color, 1.0);
}
