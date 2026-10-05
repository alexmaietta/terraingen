// Spectral shading pass: turns the G-buffer into spectral radiance, one image layer per band of
// the camera's spectral range. The host composes it with a reflectance model by prepending
//     import <model module>;
//     typealias ActiveBrdf = <model type>;
// and dispatches one pipeline per model, each shading the pixels whose material uses its model.
//
// Radiance is L = E / pi * (a * albedo + (1 - a) * F * V) + sum over lights of E_k / pi * F_k * V_k
// + albedo * L_dome * D, with E the sun's spectral irradiance at normal incidence, F the model's
// radiance factor (pi times the BRDF times cos(i)), V the shadow visibility and a the ambient
// fraction. A distant light's E_k is its irradiance; a sphere light's is its intensity over the
// squared distance (capped at its radius) times its cone's falloff. A rect or disk light's E_k is
// its radiance times the length of its vector form factor (the polygon clipped to the receiver's
// horizon, Lambert's formula), with F_k along that vector. The dome adds its radiance times its
// map's irradiance over pi, D, from spherical harmonics, diffusely. Unlit pixels get
// L = E / pi * albedo.
//
// Bindings: 0 ShadingUniforms, 1 G-buffer normal, 2 G-buffer albedo, 3 G-buffer ids, 4 depth,
// 5 materials, 6 RGB-to-spectrum coefficient table, 7 its sampler, 8 spectral radiance, 9 lights,
// 10 the lights' shadow maps, 11 their comparison sampler, 12 their view-projections.
import brdf;

static const uint kUnlit = 1;
static const uint kBackground = 0xFFFFFFFF;

struct ShadingUniforms {
    float4x4 invViewProj;
    // xyz: camera position.
    float4 eye;
    // xyz: unit direction towards the sun, w: ambient fraction.
    float4 sun;
    // First band's lower wavelength (nm), band width (nm), band count, unused.
    float4 range;
    // Image width and height, coefficient table resolution, light count.
    uint4 size;
    // Sun spectral irradiance at normal incidence per band (W m^-2 nm^-1), four bands per float4.
    float4 irradiance[16];
    // Lights' shadow filter radius (texels), normal offset (texels), map resolution, unused.
    float4 lightShadow;
    // 16 Poisson disk samples in the unit disk, two per float4.
    float4 poisson[8];
    // Dome: irradiance over pi of its RGB map as order-2 spherical harmonics (rgb), and its
    // spectral radiance per band (W m^-2 sr^-1 nm^-1), four per float4; zero without a dome.
    float4 domeSh[9];
    float4 dome[16];
}

static const uint kMaxLights = 16;

struct LightGpu {
    // xyz: position (sphere, rect, disk) or unit direction towards the light (distant), w: type,
    // 0 distant, 1 sphere, 2 rect, 3 disk.
    float4 position;
    // xyz: unit cone axis, w: sphere radius.
    float4 axis;
    // Cosine of the cone's angle (-2 without a cone), cosine where its falloff starts, first
    // shadow view, shadow view count (0 without shadows, 1, or 6 for a cube).
    float4 cone;
    // x: world size of a shadow texel, at unit distance for perspective views.
    float4 shadow;
    // Rect and disk: half width along the light's +x and half height along its +y, as vectors.
    float4 extentX;
    float4 extentY;
    // Spectral irradiance (distant, W m^-2 nm^-1) or intensity (sphere, W sr^-1 nm^-1) per band.
    float4 spectrum[16];
}

struct MaterialGpu {
    // Model index, unused.
    uint4 info;
    BrdfParams params;
    // Reflectance per band normalised to luminance 1 (G-buffer albedo.x scales it), four per
    // float4.
    float4 reflectance[16];
}

struct ShadingConstants {
    // x: index of the model this dispatch shades.
    uint4 model;
}

[[vk::binding(0, 0)]] ConstantBuffer<ShadingUniforms> sh;
[[vk::push_constant]] ConstantBuffer<ShadingConstants> pass;
[[vk::binding(1, 0)]] Texture2D<float4> gNormal;
[[vk::binding(2, 0)]] Texture2D<float4> gAlbedo;
[[vk::binding(3, 0)]] Texture2D<uint4> gIds;
[[vk::binding(4, 0)]] Texture2D<float> gDepth;
[[vk::binding(5, 0)]] StructuredBuffer<MaterialGpu> materials;
// Jakob-Hanika sigmoid coefficients (c0, c1, c2) over linear RGB in [0, 1]^3: red along x, green
// along y and blue across layers, at `size.z` samples per axis.
[[vk::binding(6, 0)]] Texture2DArray<float4> sigmoidTable;
[[vk::binding(7, 0)]] SamplerState tableSampler;
[[vk::image_format("r32f")]] [[vk::binding(8, 0)]] RWTexture2DArray<float> spectral;
[[vk::binding(9, 0)]] StructuredBuffer<LightGpu> lights;
[[vk::binding(10, 0)]] Texture2DArray<float> lightShadowMaps;
[[vk::binding(11, 0)]] SamplerComparisonState lightShadowSampler;
[[vk::binding(12, 0)]] StructuredBuffer<float4x4> lightViews;

float bandValue(float4 values[16], uint band) {
    return values[band / 4][band % 4];
}

// The modulation colour as a spectrum value at `wavelength`: its largest component times the
// sigmoid spectrum of the colour divided by it (Jakob and Hanika 2019), which reproduces the colour
// for any brightness, including modulations above 1.
float modulationAt(float3 coefficients, float scale, float wavelength) {
    let t = (wavelength - 360.0) / (830.0 - 360.0);
    let x = (coefficients.x * t + coefficients.y) * t + coefficients.z;
    return scale * (0.5 + x / (2.0 * sqrt(1.0 + x * x)));
}

// Sigmoid spectrum coefficients (Jakob and Hanika 2019) of a linear RGB colour whose largest
// component is 1, interpolated from the coefficient table.
float3 sigmoidCoefficients(float3 colour) {
    let resolution = float(sh.size.z);
    let uv = (colour.xy * (resolution - 1.0) + 0.5) / resolution;
    let layer = colour.z * (resolution - 1.0);
    let lower = floor(layer);
    let upper = min(lower + 1.0, resolution - 1.0);
    let a = sigmoidTable.SampleLevel(tableSampler, float3(uv, lower), 0).xyz;
    let b = sigmoidTable.SampleLevel(tableSampler, float3(uv, upper), 0).xyz;
    return lerp(a, b, layer - lower);
}

// The dome map's irradiance over pi at unit normal `n` (Ramamoorthi and Hanrahan 2001).
float3 domeIrradiance(float3 n) {
    let c = sh.domeSh;
    let e = c[0].rgb * 0.282095 + c[1].rgb * 0.488603 * n.y + c[2].rgb * 0.488603 * n.z +
            c[3].rgb * 0.488603 * n.x + c[4].rgb * 1.092548 * n.x * n.y +
            c[5].rgb * 1.092548 * n.y * n.z + c[6].rgb * 0.315392 * (3.0 * n.z * n.z - 1.0) +
            c[7].rgb * 1.092548 * n.x * n.z + c[8].rgb * 0.546274 * (n.x * n.x - n.y * n.y);
    return max(e, float3(0.0));
}

static const uint kDiskSides = 16;
static const uint kMaxPolygon = kDiskSides + 1;

// The vector form factor of a rect or disk light at `world` with unit normal `n`: half the sum
// over the emitter's edges, clipped to the receiver's horizon, of each edge's angle times the unit
// normal of the plane through it and `world` (Lambert's formula; Arvo 1994). Irradiance is the
// light's radiance times dot(n, V). Zero when the emitter faces away or lies below the horizon.
float3 areaFormFactor(LightGpu light, float3 world, float3 n) {
    let centre = light.position.xyz;
    if (dot(world - centre, light.axis.xyz) <= 0.0) {
        return float3(0.0);
    }
    float3 polygon[kMaxPolygon];
    uint count = 0;
    if (uint(light.position.w) == 2) {
        polygon[0] = centre - light.extentX.xyz - light.extentY.xyz;
        polygon[1] = centre + light.extentX.xyz - light.extentY.xyz;
        polygon[2] = centre + light.extentX.xyz + light.extentY.xyz;
        polygon[3] = centre - light.extentX.xyz + light.extentY.xyz;
        count = 4;
    } else {
        // A regular polygon of the disk's area.
        let step = 2.0 * PI / float(kDiskSides);
        let scale = sqrt(step / sin(step));
        for (uint i = 0; i < kDiskSides; ++i) {
            let a = step * float(i);
            polygon[i] = centre + scale * (cos(a) * light.extentX.xyz + sin(a) * light.extentY.xyz);
        }
        count = kDiskSides;
    }
    // Sutherland-Hodgman against the receiver's tangent plane.
    float3 clipped[kMaxPolygon];
    uint kept = 0;
    for (uint i = 0; i < count; ++i) {
        let a = polygon[i];
        let b = polygon[(i + 1) % count];
        let da = dot(n, a - world);
        let db = dot(n, b - world);
        if (da > 0.0) {
            clipped[kept] = a;
            kept += 1;
        }
        if ((da > 0.0) != (db > 0.0) && kept < kMaxPolygon) {
            clipped[kept] = a + (b - a) * (da / (da - db));
            kept += 1;
        }
    }
    if (kept < 3) {
        return float3(0.0);
    }
    var vector = float3(0.0);
    for (uint i = 0; i < kept; ++i) {
        let u = normalize(clipped[i] - world);
        let w = normalize(clipped[(i + 1) % kept] - world);
        let edge = cross(u, w);
        let sine = length(edge);
        if (sine > 1e-7) {
            vector += acos(clamp(dot(u, w), -1.0, 1.0)) * edge / sine;
        }
    }
    vector *= 0.5;
    return dot(vector, n) < 0.0 ? -vector : vector;
}

// Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare".
float interleavedGradientNoise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// The shadow view of a light's maps that sees `world`: its only one, or for a cube the face of
// the dominant axis from the light (faces +x, -x, +y, -y, +z, -z).
uint lightView(LightGpu light, float3 world) {
    let first = uint(light.cone.z);
    if (light.cone.w < 6.0) {
        return first;
    }
    let d = world - light.position.xyz;
    let a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        return first + (d.x >= 0.0 ? 0 : 1);
    }
    if (a.y >= a.z) {
        return first + (d.y >= 0.0 ? 2 : 3);
    }
    return first + (d.z >= 0.0 ? 4 : 5);
}

// Fraction of a light reaching `world` past its shadow map: Poisson-disk PCF with normal offset,
// as the sun's shadows (frame.slang). Points outside the map are lit.
float lightVisibility(LightGpu light, float3 world, float3 n, float nDotL, float2 pixel) {
    if (light.cone.w <= 0.0) {
        return 1.0;
    }
    let view = lightView(light, world);
    let distance = light.position.w == 0.0 ? 1.0 : length(world - light.position.xyz);
    let texelWorld = light.shadow.x * distance;
    let offset = n * sh.lightShadow.y * texelWorld * max(1.0 - clamp(nDotL, 0.0, 1.0), 0.25);
    let clip = mul(lightViews[view], float4(world + offset, 1.0));
    if (clip.w <= 0.0) {
        return 1.0;
    }
    // Receivers beyond the far plane compare at it, as the sun's (frame.slang).
    let ndc = float3(clip.xy / clip.w, min(clip.z / clip.w, 0.99999994));
    let uv = float2(0.5 * ndc.x + 0.5, 0.5 - 0.5 * ndc.y);
    if (any(uv < float2(0.0)) || any(uv > float2(1.0))) {
        return 1.0;
    }
    let angle = 6.28318530718 * interleavedGradientNoise(pixel);
    let c = cos(angle);
    let sn = sin(angle);
    let radius = sh.lightShadow.x / sh.lightShadow.z;
    var lit = 0.0;
    for (uint i = 0; i < 16; ++i) {
        let pair = sh.poisson[i / 2];
        let p = i % 2 == 0 ? pair.xy : pair.zw;
        let tap = uv + float2(c * p.x - sn * p.y, sn * p.x + c * p.y) * radius;
        lit += lightShadowMaps.SampleCmpLevelZero(lightShadowSampler, float3(tap, float(view)),
                                                  ndc.z);
    }
    return lit / 16.0;
}

// Radiance factors of the active model for three bands at once: the bands' albedos go in the RGB
// slots of BrdfInput, which models treat independently.
float3 radianceFactors(float3 n, float3 l, float3 v, float3 albedo, BrdfParams params) {
    let input = brdfInputOf(n, l, v, albedo);
    if (input.mu0 <= 0.0) {
        return float3(0.0);
    }
    ActiveBrdf model;
    return max(model.radianceFactor(input, params), float3(0.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void shadeMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= sh.size.x || id.y >= sh.size.y) {
        return;
    }
    let pixel = int3(id.xy, 0);
    let ids = gIds.Load(pixel);
    if (ids.x == kBackground) {
        return;
    }
    let material = materials[ids.x];
    if (material.info.x != pass.model.x) {
        return;
    }
    let ndc = float3((float(id.x) + 0.5) / float(sh.size.x) * 2.0 - 1.0,
                     1.0 - (float(id.y) + 0.5) / float(sh.size.y) * 2.0, gDepth.Load(pixel));
    let clip = mul(sh.invViewProj, float4(ndc, 1.0));
    let world = clip.xyz / clip.w;
    let normalVisibility = gNormal.Load(pixel);
    let n = normalize(normalVisibility.xyz);
    let visibility = normalVisibility.w;
    let albedo = gAlbedo.Load(pixel);
    let modulation = max(albedo.yzw, float3(0.0));
    let scale = max(modulation.x, max(modulation.y, modulation.z));
    let coefficients = scale > 0.0 ? sigmoidCoefficients(modulation / scale) : float3(0.0);
    let v = normalize(sh.eye.xyz - world);
    let l = sh.sun.xyz;
    let ambient = sh.sun.w;
    let bands = uint(sh.range.z);
    let unlit = (ids.w & kUnlit) != 0;
    let domeRgb = unlit ? float3(0.0) : domeIrradiance(n);
    let domeScale = max(domeRgb.x, max(domeRgb.y, domeRgb.z));
    let domeCoefficients = domeScale > 0.0 ? sigmoidCoefficients(domeRgb / domeScale) : float3(0.0);

    // Per light: direction towards it and the scale on its spectrum (inverse square, cone and
    // shadow); 0 for lights that cannot reach the point.
    let lightCount = unlit ? 0 : min(sh.size.w, kMaxLights);
    float3 lightDirections[kMaxLights];
    float lightScales[kMaxLights];
    for (uint k = 0; k < lightCount; ++k) {
        let light = lights[k];
        var toLight = light.position.xyz;
        var scale = 1.0;
        let type = uint(light.position.w);
        if (type >= 2) {
            let form = areaFormFactor(light, world, n);
            scale = length(form);
            toLight = scale > 0.0 ? form / scale : n;
        } else if (type == 1) {
            let offset = light.position.xyz - world;
            let distance2 = max(dot(offset, offset), 1e-12);
            toLight = offset * rsqrt(distance2);
            scale = 1.0 / max(distance2, light.axis.w * light.axis.w);
            if (light.cone.x > -1.5) {
                scale *= smoothstep(light.cone.x, max(light.cone.y, light.cone.x + 1e-6),
                                    dot(-toLight, light.axis.xyz));
            }
        }
        let nDotL = dot(n, toLight);
        if (nDotL <= 0.0 || scale <= 0.0) {
            scale = 0.0;
        } else {
            scale *= lightVisibility(light, world, n, nDotL, float2(id.xy));
        }
        lightDirections[k] = toLight;
        lightScales[k] = scale;
    }
    for (uint first = 0; first < bands; first += 3) {
        float3 bandAlbedo = float3(0.0);
        float3 bandIrradiance = float3(0.0);
        for (uint k = 0; k < 3; ++k) {
            let band = min(first + k, bands - 1);
            let wavelength = sh.range.x + (float(band) + 0.5) * sh.range.y;
            bandAlbedo[k] = bandValue(material.reflectance, band) * albedo.x *
                            modulationAt(coefficients, scale, wavelength);
            bandIrradiance[k] = bandValue(sh.irradiance, band);
        }
        float3 response;
        if (unlit) {
            response = bandAlbedo;
        } else {
            let factors = radianceFactors(n, l, v, bandAlbedo, material.params);
            response = ambient * bandAlbedo + (1.0 - ambient) * factors * visibility;
        }
        var radiance = bandIrradiance / PI * response;
        if (domeScale > 0.0) {
            for (uint j = 0; j < 3; ++j) {
                let band = min(first + j, bands - 1);
                let wavelength = sh.range.x + (float(band) + 0.5) * sh.range.y;
                radiance[j] += bandAlbedo[j] * bandValue(sh.dome, band) *
                               modulationAt(domeCoefficients, domeScale, wavelength);
            }
        }
        for (uint k = 0; k < lightCount; ++k) {
            if (lightScales[k] <= 0.0) {
                continue;
            }
            float3 lightSpectrum;
            for (uint j = 0; j < 3; ++j) {
                lightSpectrum[j] = bandValue(lights[k].spectrum, min(first + j, bands - 1));
            }
            let factors = radianceFactors(n, lightDirections[k], v, bandAlbedo, material.params);
            radiance += lightScales[k] * lightSpectrum / PI * factors;
        }
        for (uint k = 0; k < 3 && first + k < bands; ++k) {
            spectral[int3(id.xy, first + k)] = radiance[k];
        }
    }
}
