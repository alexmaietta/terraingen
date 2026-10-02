// Terrain colour pass. The host composes it with a reflectance model by prepending
//     import <model module>;
//     typealias ActiveBrdf = <model type>;
import brdf;
import terrain;
import surface;

struct Shadows {
    float4x4 lightViewProj[4];
    // Far view depth of each cascade.
    float4 splits;
    // World size of one shadow map texel in each cascade.
    float4 texelWorld;
    // Filter radius (texels), normal offset (texels), cascade count, enabled.
    float4 params;
    // x: tint cascades.
    uint4 debug;
    // 16 Poisson disk samples in the unit disk, two per float4.
    float4 poisson[8];
}

[[vk::binding(5, 0)]] ConstantBuffer<Shadows> s;
[[vk::binding(6, 0)]] Texture2DArray<float> shadowMap;
[[vk::binding(7, 0)]] SamplerComparisonState shadowSampler;

struct VertexOutput {
    float4 position : SV_Position;
    float3 world : WORLD;
    float height : HEIGHT;
    float3 barycentric : BARYCENTRIC;
    nointerpolation uint level : LEVEL;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID) {
    let barycentric = float3[6](float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1), float3(1, 0, 0),
                                float3(0, 1, 0), float3(0, 0, 1));
    let v = gridVertex(vertexIndex, instanceIndex);
    VertexOutput out;
    out.position = mul(u.viewProj, float4(v.world, 1.0));
    out.world = v.world;
    out.height = v.height;
    out.barycentric = barycentric[v.corner];
    out.level = v.level;
    return out;
}

// Radiance factor of the active reflectance model for unit normal n, unit direction to the light l
// and to the viewer v.
float3 reflected(float3 n, float3 l, float3 v, float3 albedo) {
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
    ActiveBrdf model;
    return max(model.radianceFactor(input, surface.brdf), float3(0.0));
}

float3 colormap(float t) {
    let stops = float3[5](float3(0.16, 0.18, 0.30), float3(0.20, 0.42, 0.52),
                          float3(0.56, 0.62, 0.44), float3(0.80, 0.72, 0.52),
                          float3(0.96, 0.95, 0.92));
    let x = clamp(t, 0.0, 1.0) * 4.0;
    let i = uint(min(floor(x), 3.0));
    return lerp(stops[i], stops[i + 1], x - float(i));
}

// Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare".
float interleavedGradientNoise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float2 poissonSample(uint i) {
    let pair = s.poisson[i / 2];
    return i % 2 == 0 ? pair.xy : pair.zw;
}

uint cascadeIndex(float viewDepth) {
    let count = uint(s.params.z);
    for (uint c = 0; c < count; ++c) {
        if (viewDepth < s.splits[c]) {
            return c;
        }
    }
    return count;
}

// Poisson-disk PCF over a cascaded shadow map. Each tap is a hardware 2x2 comparison; the disk is
// rotated per pixel so undersampling shows as fine noise rather than banding. Normal offset
// (Holbert 2011) pushes the lookup off the surface to avoid self-shadowing acne.
float shadowVisibility(float3 world, float3 normal, float nDotL, float viewDepth, float2 pixel) {
    let cascade = cascadeIndex(viewDepth);
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

[shader("fragment")]
float4 fragmentMain(VertexOutput in) : SV_Target {
    let edgeWidth = fwidth(in.barycentric);
    let detail = sampleDetail(detailCoords(in.world));

    // Shadow lookups use the terrain normal; shading adds the detail normal's slope.
    let slope = terrainSlope(in.world);
    let normal = slopeNormal(slope);
    let shadingNormal = slopeNormal(slope + detail.slope);
    let nDotL = dot(normal, u.sun.xyz);
    var visibility = 1.0;
    if (s.params.w > 0.0) {
        if (nDotL <= 0.0) {
            visibility = 0.0;
        } else {
            let viewDepth = dot(in.world, u.forward.xyz) - u.forward.w;
            visibility = shadowVisibility(in.world, normal, nDotL, viewDepth, in.position.xy);
        }
    }
    let albedo = surfaceAlbedo(in.world) * detail.albedo;
    let toEye = normalize(u.eye.xyz - in.world);
    let lit = reflected(shadingNormal, u.sun.xyz, toEye, albedo) * visibility * surface.albedo.a;
    let shade = u.sun.w * albedo + (1.0 - u.sun.w) * lit;
    let range = max(u.heightRange.y - u.heightRange.x, 1e-6);
    let t = (in.height - u.heightRange.x) / range;

    var color = shade;
    if (u.dims.z == 1) {
        color = colormap(t);
    } else if (u.dims.z == 2) {
        color = colormap(t) * shade;
    }

    if (s.debug.x == 1 && s.params.w > 0.0) {
        let tints = float3[5](float3(1.0, 0.55, 0.55), float3(0.55, 1.0, 0.55),
                              float3(0.55, 0.65, 1.0), float3(1.0, 1.0, 0.5), float3(1.0, 1.0, 1.0));
        let viewDepth = dot(in.world, u.forward.xyz) - u.forward.w;
        color = color * tints[cascadeIndex(viewDepth)];
    }

    if (u.lodInfo.y == 1) {
        let levelTints = float3[6](float3(1.0, 0.6, 0.6), float3(0.6, 1.0, 0.6),
                                   float3(0.6, 0.7, 1.0), float3(1.0, 1.0, 0.55),
                                   float3(1.0, 0.6, 1.0), float3(0.55, 1.0, 1.0));
        color = color * levelTints[in.level % 6];
    }

    if (u.dims.w == 1) {
        let edge = smoothstep(float3(0.0), 1.5 * edgeWidth, in.barycentric);
        let line = 1.0 - min(edge.x, min(edge.y, edge.z));
        color = lerp(color, float3(0.05, 0.05, 0.06), line);
    }
    return float4(color, 1.0);
}
