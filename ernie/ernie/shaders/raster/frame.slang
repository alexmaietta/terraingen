// Per-frame resources every raster pass shares, at fixed bindings of its push-descriptor set:
//   0 Uniforms (view and light), 1 Shadows, 2 cascaded shadow maps, 3 shadow comparison sampler,
//   4 clamping linear sampler;
// the per-draw push constants; and sun visibility and reflectance helpers. Materials take
// bindings 5 to 9 (surface.slang) and each part's own resources start at 10.
import brdf;

public struct Uniforms {
    public float4x4 viewProj;
    // xyz: unit direction towards the sun, w: ambient light fraction.
    public float4 sun;
    // xyz: camera forward, w: dot(eye, forward); view depth = dot(world, forward) - w.
    public float4 forward;
    // xyz: camera position, w: exposure, a scale on reflected sunlight.
    public float4 eye;
    // x: wireframe.
    public uint4 flags;
}

// Per draw: the light's view-projection for shadow passes, and the pass's first item (terrain
// patch or mesh draw).
public struct DrawConstants {
    public float4x4 lightViewProj;
    public uint first;
}

public struct Shadows {
    public float4x4 lightViewProj[4];
    // Far view depth of each cascade.
    public float4 splits;
    // World size of one shadow map texel in each cascade.
    public float4 texelWorld;
    // Filter radius (texels), normal offset (texels), cascade count, enabled.
    public float4 params;
    // x: tint cascades.
    public uint4 debug;
    // 16 Poisson disk samples in the unit disk, two per float4.
    public float4 poisson[8];
}

[[vk::binding(0, 0)]] public ConstantBuffer<Uniforms> u;
[[vk::binding(1, 0)]] public ConstantBuffer<Shadows> s;
[[vk::binding(2, 0)]] public Texture2DArray<float> shadowMap;
[[vk::binding(3, 0)]] public SamplerComparisonState shadowSampler;
[[vk::binding(4, 0)]] public SamplerState clampSampler;
[[vk::push_constant]] public ConstantBuffer<DrawConstants> draw;

// Radiance factor of reflectance model B for unit normal n, unit direction to the light l and to
// the viewer v.
public float3 reflected<B : IBrdf>(float3 n, float3 l, float3 v, float3 albedo, BrdfParams params) {
    let mu0 = dot(n, l);
    if (mu0 <= 0.0) {
        return float3(0.0);
    }
    let mu = dot(n, v);
    let lPlane = l - n * mu0;
    let vPlane = v - n * mu;
    var cosPsi = 1.0;
    if (length(lPlane) > 1e-5 && length(vPlane) > 1e-5) {
        cosPsi = dot(normalize(lPlane), normalize(vPlane));
    }
    BrdfInput input;
    input.mu0 = mu0;
    input.mu = max(mu, 1e-3);
    input.cosG = dot(l, v);
    input.cosPsi = cosPsi;
    input.albedo = albedo;
    B model;
    return max(model.radianceFactor(input, params), float3(0.0));
}

// Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare".
float interleavedGradientNoise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float2 poissonSample(uint i) {
    let pair = s.poisson[i / 2];
    return i % 2 == 0 ? pair.xy : pair.zw;
}

public float viewDepth(float3 world) {
    return dot(world, u.forward.xyz) - u.forward.w;
}

public uint cascadeIndex(float depth) {
    let count = uint(s.params.z);
    for (uint c = 0; c < count; ++c) {
        if (depth < s.splits[c]) {
            return c;
        }
    }
    return count;
}

// Poisson-disk PCF over a cascaded shadow map. Each tap is a hardware 2x2 comparison; the disk is
// rotated per pixel so undersampling shows as fine noise rather than banding. Normal offset
// (Holbert 2011) pushes the lookup off the surface to avoid self-shadowing acne.
float shadowVisibility(float3 world, float3 normal, float nDotL, float depth, float2 pixel) {
    let cascade = cascadeIndex(depth);
    if (cascade >= uint(s.params.z)) {
        return 1.0;
    }
    let texelWorld = s.texelWorld[cascade];
    // A floor on the offset keeps surfaces facing the light head-on, which get no slope-scaled
    // bias, from comparing equal to their own depth.
    let offset = normal * s.params.y * texelWorld * max(1.0 - clamp(nDotL, 0.0, 1.0), 0.25);
    let clip = mul(s.lightViewProj[cascade], float4(world + offset, 1.0));
    let ndc = clip.xyz / clip.w;
    let uv = float2(0.5 * ndc.x + 0.5, 0.5 - 0.5 * ndc.y);
    if (any(uv < float2(0.0)) || any(uv > float2(1.0)) || ndc.z > 1.0) {
        return 1.0;
    }

    let angle = 6.28318530718 * interleavedGradientNoise(pixel);
    let c = cos(angle);
    let sn = sin(angle);
    uint width;
    uint height;
    uint layers;
    shadowMap.GetDimensions(width, height, layers);
    let radius = s.params.x / float(width);
    var lit = 0.0;
    for (uint i = 0; i < 16; ++i) {
        let p = poissonSample(i);
        let tap = uv + float2(c * p.x - sn * p.y, sn * p.x + c * p.y) * radius;
        lit += shadowMap.SampleCmpLevelZero(shadowSampler, float3(tap, float(cascade)), ndc.z);
    }
    return lit / 16.0;
}

// Fraction of the sun reaching a surface point with unit geometric normal `normal`, at pixel
// `pixel`: 0 facing away from the sun, shadow-mapped otherwise when shadows are on.
public float sunVisibility(float3 world, float3 normal, float2 pixel) {
    if (s.params.w <= 0.0) {
        return 1.0;
    }
    let nDotL = dot(normal, u.sun.xyz);
    if (nDotL <= 0.0) {
        return 0.0;
    }
    return shadowVisibility(world, normal, nDotL, viewDepth(world), pixel);
}

// `color` tinted by shadow cascade when cascade debugging is on.
public float3 cascadeTint(float3 color, float3 world) {
    if (s.debug.x == 1 && s.params.w > 0.0) {
        let tints = float3[5](float3(1.0, 0.55, 0.55), float3(0.55, 1.0, 0.55),
                              float3(0.55, 0.65, 1.0), float3(1.0, 1.0, 0.5), float3(1.0, 1.0, 1.0));
        return color * tints[cascadeIndex(viewDepth(world))];
    }
    return color;
}

// `color` with triangle edges drawn over it when wireframe is on, from barycentric coordinates.
public float3 wireframe(float3 color, float3 barycentric) {
    if (u.flags.x == 1) {
        let edge = smoothstep(float3(0.0), 1.5 * fwidth(barycentric), barycentric);
        let line = 1.0 - min(edge.x, min(edge.y, edge.z));
        return lerp(color, float3(0.05, 0.05, 0.06), line);
    }
    return color;
}
