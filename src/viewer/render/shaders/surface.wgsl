struct Surface {
    // rgb: albedo where no albedo map applies, a: exposure.
    albedo: vec4<f32>,
    // Albedo map: origin x, origin y (top edge), pixel size, 1 when the map is used.
    albedo_map: vec4<f32>,
    // Albedo map width and height in cells, detail tiles per world unit, normal green sign
    // (+1 OpenGL, -1 DirectX).
    albedo_map_size: vec4<f32>,
    // Detail albedo strength, detail normal strength, has detail albedo, has detail normal.
    detail: vec4<f32>,
    // rgb: mean linear colour of the detail albedo texture, a: unused.
    detail_mean: vec4<f32>,
    // Hex tiling: enabled, rotation strength, luminance contrast, blend exponent.
    hex: vec4<f32>,
    // Reflectance model parameters, four per vec4.
    params: array<vec4<f32>, 4>,
}

@group(2) @binding(0) var<uniform> surface: Surface;
// Albedo map: r albedo times validity, g validity, so filtering fades to the constant albedo at
// gaps.
@group(2) @binding(1) var albedo_map: texture_2d<f32>;
@group(2) @binding(2) var detail_albedo: texture_2d<f32>;
@group(2) @binding(3) var detail_normal: texture_2d<f32>;
@group(2) @binding(4) var detail_sampler: sampler;

fn brdf_param(i: u32) -> f32 {
    let packed = surface.params[i / 4u];
    return packed[i % 4u];
}

fn surface_albedo(world: vec3<f32>) -> vec3<f32> {
    let constant = surface.albedo.rgb;
    if (surface.albedo_map.w == 0.0) {
        return constant;
    }
    let extent = surface.albedo_map_size.xy * surface.albedo_map.z;
    let uv = vec2<f32>(world.x - surface.albedo_map.x, surface.albedo_map.y - world.y) / extent;
    let m = textureSampleLevel(albedo_map, gradient_sampler, uv, 0.0).rg;
    return vec3<f32>(m.r) + (1.0 - m.g) * constant;
}

// Detail texture coordinates: world x east and y north scaled to tiles. Textures are sampled with
// v = -y so image rows run southwards and the image's up direction points north.
struct DetailCoords {
    tile: vec2<f32>,
    dx: vec2<f32>,
    dy: vec2<f32>,
}

fn detail_coords(world: vec3<f32>) -> DetailCoords {
    var c: DetailCoords;
    c.tile = world.xy * surface.albedo_map_size.z;
    c.dx = dpdx(c.tile);
    c.dy = dpdy(c.tile);
    return c;
}

struct DetailSample {
    // Multiplier on the base albedo.
    albedo: vec3<f32>,
    // Slope (dz/dx east, dz/dy north) added to the terrain's.
    slope: vec2<f32>,
}

fn to_texture(tile: vec2<f32>) -> vec2<f32> {
    return vec2<f32>(tile.x, -tile.y);
}

fn rotate(v: vec2<f32>, cs: vec2<f32>) -> vec2<f32> {
    return vec2<f32>(cs.x * v.x - cs.y * v.y, cs.y * v.x + cs.x * v.y);
}

struct DetailTap {
    albedo: vec3<f32>,
    slope: vec2<f32>,
}

// One tap of both detail textures at `tile` (tile space, y north) with derivatives, its tangent
// slope rotated back by `inverse` (cos, sin) into tile space.
fn detail_tap(tile: vec2<f32>, dx: vec2<f32>, dy: vec2<f32>, inverse: vec2<f32>) -> DetailTap {
    var t: DetailTap;
    t.albedo = vec3<f32>(1.0);
    t.slope = vec2<f32>(0.0);
    if (surface.detail.z > 0.0) {
        t.albedo = textureSampleGrad(detail_albedo, detail_sampler, to_texture(tile),
                                     to_texture(dx), to_texture(dy)).rgb;
    }
    if (surface.detail.w > 0.0) {
        var n = textureSampleGrad(detail_normal, detail_sampler, to_texture(tile), to_texture(dx),
                                  to_texture(dy)).xyz * 2.0 - 1.0;
        n.y = n.y * surface.albedo_map_size.w;
        t.slope = rotate(-n.xy / max(n.z, 0.05), inverse);
    }
    return t;
}

// pcg2d (Jarzynski and Olano 2020).
fn pcg2d(seed: vec2<u32>) -> vec2<u32> {
    var v = seed * 1664525u + 1013904223u;
    v.x = v.x + v.y * 1664525u;
    v.y = v.y + v.x * 1664525u;
    v = v ^ (v >> vec2<u32>(16u));
    v.x = v.x + v.y * 1664525u;
    v.y = v.y + v.x * 1664525u;
    v = v ^ (v >> vec2<u32>(16u));
    return v;
}

// Hex-tiling (Mikkelsen 2022): the plane is covered by a triangle grid whose vertices are hexagon
// centres; each vertex samples the texture with its own random rotation and offset, and the three
// samples are blended with barycentric weights sharpened by an exponent and biased towards brighter
// samples, which hides the seams without visible blur. Slopes from the normal map are rotated back
// with each sample's rotation so lighting stays consistent across tiles.
fn hex_detail(tile: vec2<f32>, dx: vec2<f32>, dy: vec2<f32>) -> DetailTap {
    // Skew into the simplex triangle grid with unit edges of the hexagon lattice.
    let st = tile * 2.0 * sqrt(3.0);
    let skewed = vec2<f32>(st.x - 0.57735027 * st.y, 1.15470054 * st.y);
    let base = vec2<i32>(floor(skewed));
    let f = fract(skewed);
    let z = 1.0 - f.x - f.y;
    let upper = select(0.0, 1.0, z < 0.0);
    let flip = 2.0 * upper - 1.0;
    let weights = vec3<f32>(-z * flip, upper - f.y * flip, upper - f.x * flip);
    let u = i32(upper);
    var vertices = array<vec2<i32>, 3>(base + vec2<i32>(u, u), base + vec2<i32>(u, 1 - u),
                                       base + vec2<i32>(1 - u, u));

    var taps: array<DetailTap, 3>;
    var luminance = vec3<f32>(1.0);
    for (var k = 0u; k < 3u; k = k + 1u) {
        let vertex = vertices[k];
        let centre = vec2<f32>(f32(vertex.x) + 0.5 * f32(vertex.y), f32(vertex.y) / 1.15470054) /
                     (2.0 * sqrt(3.0));
        let h = pcg2d(bitcast<vec2<u32>>(vertex));
        let angle = (f32(h.x) / 4294967295.0 * 2.0 - 1.0) * PI * surface.hex.y;
        let cs = vec2<f32>(cos(angle), sin(angle));
        let offset = vec2<f32>(pcg2d(h).xy) / 4294967295.0;
        let rotated = rotate(tile - centre, cs) + centre + offset;
        taps[k] = detail_tap(rotated, rotate(dx, cs), rotate(dy, cs), vec2<f32>(cs.x, -cs.y));
        luminance[k] = dot(taps[k].albedo, vec3<f32>(0.299, 0.587, 0.114));
    }
    // Weights relative to the largest one keep the dominant term at 1, so high exponents cannot
    // underflow the normalisation.
    let contrast = mix(vec3<f32>(1.0), luminance, surface.hex.z);
    let largest = max(weights.x, max(weights.y, weights.z));
    var w = contrast * pow(weights / largest, vec3<f32>(surface.hex.w));
    w = w / max(w.x + w.y + w.z, 1e-6);

    var result: DetailTap;
    result.albedo = w.x * taps[0].albedo + w.y * taps[1].albedo + w.z * taps[2].albedo;
    result.slope = w.x * taps[0].slope + w.y * taps[1].slope + w.z * taps[2].slope;
    return result;
}

fn sample_detail(c: DetailCoords) -> DetailSample {
    var tap: DetailTap;
    if (surface.hex.x > 0.0) {
        tap = hex_detail(c.tile, c.dx, c.dy);
    } else {
        tap = detail_tap(c.tile, c.dx, c.dy, vec2<f32>(1.0, 0.0));
    }
    var d: DetailSample;
    let normalised = tap.albedo / max(surface.detail_mean.rgb, vec3<f32>(1e-4));
    d.albedo = mix(vec3<f32>(1.0), normalised, surface.detail.x * surface.detail.z);
    d.slope = tap.slope * surface.detail.y;
    return d;
}
