// Terrain surface: material uniform, albedo map, and tileable detail albedo and normal textures
// with hex-tiling.
import brdf;
import terrain;

public struct Surface {
    // rgb: albedo where no albedo map applies, a: exposure.
    public float4 albedo;
    // Albedo map: origin x, origin y (top edge), pixel size, 1 when the map is used.
    public float4 albedoMap;
    // Albedo map width and height in cells, detail tiles per world unit, normal green sign
    // (+1 OpenGL, -1 DirectX).
    public float4 albedoMapSize;
    // Detail albedo strength, detail normal strength, has detail albedo, has detail normal.
    public float4 detail;
    // rgb: mean linear colour of the detail albedo texture, a: unused.
    public float4 detailMean;
    // Hex tiling: enabled, rotation strength, luminance contrast, blend exponent.
    public float4 hex;
    // Parameters of the active reflectance model.
    public BrdfParams brdf;
}

[[vk::binding(8, 0)]] public ConstantBuffer<Surface> surface;
// Albedo map: r albedo times validity, g validity, so filtering fades to the constant albedo at
// gaps.
[[vk::binding(9, 0)]] public Texture2D<float2> albedoMap;
[[vk::binding(10, 0)]] public Texture2D<float4> detailAlbedo;
[[vk::binding(11, 0)]] public Texture2D<float4> detailNormal;
[[vk::binding(12, 0)]] public SamplerState detailSampler;

public float3 surfaceAlbedo(float3 world) {
    let constant = surface.albedo.rgb;
    if (surface.albedoMap.w == 0.0) {
        return constant;
    }
    let extent = surface.albedoMapSize.xy * surface.albedoMap.z;
    let uv = float2(world.x - surface.albedoMap.x, surface.albedoMap.y - world.y) / extent;
    let m = albedoMap.SampleLevel(clampSampler, uv, 0);
    return float3(m.r) + (1.0 - m.g) * constant;
}

// Detail texture coordinates: world x east and y north scaled to tiles. Textures are sampled with
// v = -y so image rows run southwards and the image's up direction points north.
public struct DetailCoords {
    public float2 tile;
    public float2 dx;
    public float2 dy;
}

public DetailCoords detailCoords(float3 world) {
    DetailCoords c;
    c.tile = world.xy * surface.albedoMapSize.z;
    c.dx = ddx(c.tile);
    c.dy = ddy(c.tile);
    return c;
}

public struct DetailSample {
    // Multiplier on the base albedo.
    public float3 albedo;
    // Slope (dz/dx east, dz/dy north) added to the terrain's.
    public float2 slope;
}

float2 toTexture(float2 tile) {
    return float2(tile.x, -tile.y);
}

float2 rotate(float2 v, float2 cs) {
    return float2(cs.x * v.x - cs.y * v.y, cs.y * v.x + cs.x * v.y);
}

public struct DetailTap {
    public float3 albedo;
    public float2 slope;
}

// One tap of both detail textures at `tile` (tile space, y north) with derivatives, its tangent
// slope rotated back by `inverse` (cos, sin) into tile space.
DetailTap detailTap(float2 tile, float2 dx, float2 dy, float2 inverse) {
    DetailTap t;
    t.albedo = float3(1.0);
    t.slope = float2(0.0);
    if (surface.detail.z > 0.0) {
        t.albedo = detailAlbedo
                       .SampleGrad(detailSampler, toTexture(tile), toTexture(dx), toTexture(dy))
                       .rgb;
    }
    if (surface.detail.w > 0.0) {
        var n = detailNormal.SampleGrad(detailSampler, toTexture(tile), toTexture(dx),
                                        toTexture(dy)).xyz * 2.0 - 1.0;
        n.y = n.y * surface.albedoMapSize.w;
        t.slope = rotate(-n.xy / max(n.z, 0.05), inverse);
    }
    return t;
}

// pcg2d (Jarzynski and Olano 2020).
uint2 pcg2d(uint2 seed) {
    var v = seed * 1664525u + 1013904223u;
    v.x = v.x + v.y * 1664525u;
    v.y = v.y + v.x * 1664525u;
    v = v ^ (v >> 16u);
    v.x = v.x + v.y * 1664525u;
    v.y = v.y + v.x * 1664525u;
    v = v ^ (v >> 16u);
    return v;
}

// Hex-tiling (Mikkelsen 2022): the plane is covered by a triangle grid whose vertices are hexagon
// centres; each vertex samples the texture with its own random rotation and offset, and the three
// samples are blended with barycentric weights sharpened by an exponent and biased towards brighter
// samples, which hides the seams without visible blur. Slopes from the normal map are rotated back
// with each sample's rotation so lighting stays consistent across tiles.
public DetailTap hexDetail(float2 tile, float2 dx, float2 dy) {
    // Skew into the simplex triangle grid with unit edges of the hexagon lattice.
    let st = tile * 2.0 * sqrt(3.0);
    let skewed = float2(st.x - 0.57735027 * st.y, 1.15470054 * st.y);
    let base = int2(floor(skewed));
    let f = frac(skewed);
    let z = 1.0 - f.x - f.y;
    let upper = z < 0.0 ? 1.0 : 0.0;
    let flip = 2.0 * upper - 1.0;
    let weights = float3(-z * flip, upper - f.y * flip, upper - f.x * flip);
    let iu = int(upper);
    let vertices = int2[3](base + int2(iu, iu), base + int2(iu, 1 - iu), base + int2(1 - iu, iu));

    DetailTap taps[3];
    var luminance = float3(1.0);
    for (uint k = 0; k < 3; ++k) {
        let vertex = vertices[k];
        let centre = float2(float(vertex.x) + 0.5 * float(vertex.y), float(vertex.y) / 1.15470054) /
                     (2.0 * sqrt(3.0));
        let h = pcg2d(asuint(vertex));
        let angle = (float(h.x) / 4294967295.0 * 2.0 - 1.0) * PI * surface.hex.y;
        let cs = float2(cos(angle), sin(angle));
        let offset = float2(pcg2d(h)) / 4294967295.0;
        let rotated = rotate(tile - centre, cs) + centre + offset;
        taps[k] = detailTap(rotated, rotate(dx, cs), rotate(dy, cs), float2(cs.x, -cs.y));
        luminance[k] = dot(taps[k].albedo, float3(0.299, 0.587, 0.114));
    }
    // Weights relative to the largest one keep the dominant term at 1, so high exponents cannot
    // underflow the normalisation.
    let contrast = lerp(float3(1.0), luminance, surface.hex.z);
    let largest = max(weights.x, max(weights.y, weights.z));
    var w = contrast * pow(weights / largest, float3(surface.hex.w));
    w = w / max(w.x + w.y + w.z, 1e-6);

    DetailTap result;
    result.albedo = w.x * taps[0].albedo + w.y * taps[1].albedo + w.z * taps[2].albedo;
    result.slope = w.x * taps[0].slope + w.y * taps[1].slope + w.z * taps[2].slope;
    return result;
}

public DetailSample sampleDetail(DetailCoords c) {
    DetailTap tap;
    if (surface.hex.x > 0.0) {
        tap = hexDetail(c.tile, c.dx, c.dy);
    } else {
        tap = detailTap(c.tile, c.dx, c.dy, float2(1.0, 0.0));
    }
    DetailSample d;
    let normalised = tap.albedo / max(surface.detailMean.rgb, float3(1e-4));
    d.albedo = lerp(float3(1.0), normalised, surface.detail.x * surface.detail.z);
    d.slope = tap.slope * surface.detail.y;
    return d;
}
