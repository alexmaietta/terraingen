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

// Central-difference height gradient (dz/dx east, dz/dy north) per raster cell, before
// exaggeration; filterable, unlike `heights`.
@group(0) @binding(3) var gradients: texture_2d<f32>;
@group(0) @binding(4) var gradient_sampler: sampler;

@group(1) @binding(0) var<uniform> s: Shadows;
@group(1) @binding(1) var shadow_map: texture_depth_2d_array;
@group(1) @binding(2) var shadow_sampler: sampler_comparison;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) world: vec3<f32>,
    @location(1) height: f32,
    @location(2) barycentric: vec3<f32>,
    @location(3) @interpolate(flat) level: u32,
}

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32,
           @builtin(instance_index) instance_index: u32) -> VertexOutput {
    var barycentric = array<vec3<f32>, 6>(
        vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0),
        vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0));
    let v = grid_vertex(vertex_index, instance_index);
    var out: VertexOutput;
    out.position = u.view_proj * vec4<f32>(v.world, 1.0);
    out.world = v.world;
    out.height = v.height;
    out.barycentric = barycentric[v.corner];
    out.level = v.level;
    return out;
}

// Exaggerated terrain slope (dz/dx, dz/dy) from the full-resolution gradient at the fragment's
// position rather than from the mesh, so shading does not change as LOD levels morph.
fn terrain_slope(world: vec3<f32>) -> vec2<f32> {
    let extent = vec2<f32>(u.dims.xy) * u.grid.z;
    let uv = vec2<f32>(world.x - u.grid.x, u.grid.y - world.y) / extent;
    return textureSampleLevel(gradients, gradient_sampler, uv, 0.0).xy * u.grid.w;
}

fn slope_normal(slope: vec2<f32>) -> vec3<f32> {
    return normalize(vec3<f32>(-slope.x, -slope.y, 1.0));
}

// Radiance factor of the selected reflectance model for unit normal n, unit direction to the light
// l and to the viewer v.
fn reflected(n: vec3<f32>, l: vec3<f32>, v: vec3<f32>, albedo: vec3<f32>) -> vec3<f32> {
    let mu0 = dot(n, l);
    if (mu0 <= 0.0) {
        return vec3<f32>(0.0);
    }
    let mu = dot(n, v);
    let l_plane = l - n * mu0;
    let v_plane = v - n * mu;
    var cos_psi = 1.0;
    if (length(l_plane) > 1e-5 && length(v_plane) > 1e-5) {
        cos_psi = dot(normalize(l_plane), normalize(v_plane));
    }
    var input: BrdfInput;
    input.mu0 = mu0;
    input.mu = max(mu, 1e-3);
    input.cos_g = dot(l, v);
    input.cos_psi = cos_psi;
    input.albedo = albedo;
    return max(brdf_radiance_factor(input), vec3<f32>(0.0));
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
    // A floor on the offset keeps surfaces facing the light head-on, which get no slope-scaled
    // bias, from comparing equal to their own depth.
    let offset = normal * s.params.y * texel_world * max(1.0 - clamp(n_dot_l, 0.0, 1.0), 0.25);
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
    let detail = sample_detail(detail_coords(in.world));

    // Shadow lookups use the terrain normal; shading adds the detail normal's slope.
    let slope = terrain_slope(in.world);
    let normal = slope_normal(slope);
    let shading_normal = slope_normal(slope + detail.slope);
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
    let albedo = surface_albedo(in.world) * detail.albedo;
    let to_eye = normalize(u.eye.xyz - in.world);
    let lit = reflected(shading_normal, u.sun.xyz, to_eye, albedo) * visibility * surface.albedo.a;
    let shade = u.sun.w * albedo + (1.0 - u.sun.w) * lit;
    let range = max(u.height_range.y - u.height_range.x, 1e-6);
    let t = (in.height - u.height_range.x) / range;

    var color = shade;
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

    if (u.lod_info.y == 1u) {
        var level_tints = array<vec3<f32>, 6>(
            vec3<f32>(1.0, 0.6, 0.6), vec3<f32>(0.6, 1.0, 0.6), vec3<f32>(0.6, 0.7, 1.0),
            vec3<f32>(1.0, 1.0, 0.55), vec3<f32>(1.0, 0.6, 1.0), vec3<f32>(0.55, 1.0, 1.0));
        color = color * level_tints[in.level % 6u];
    }

    if (u.dims.w == 1u) {
        let edge = smoothstep(vec3<f32>(0.0), 1.5 * edge_width, in.barycentric);
        let line = 1.0 - min(edge.x, min(edge.y, edge.z));
        color = mix(color, vec3<f32>(0.05, 0.05, 0.06), line);
    }
    return vec4<f32>(color, 1.0);
}
