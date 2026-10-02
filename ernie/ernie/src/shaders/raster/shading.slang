// Spectral shading pass: turns the G-buffer into spectral radiance, one image layer per band of
// the camera's spectral range. The host composes it with a reflectance model by prepending
//     import <model module>;
//     typealias ActiveBrdf = <model type>;
// and dispatches one pipeline per model, each shading the pixels whose material uses its model.
//
// Radiance is L = E / pi * (a * albedo + (1 - a) * F * V), with E the sun's spectral irradiance at
// normal incidence, F the model's radiance factor (pi times the BRDF times cos(i)), V the shadow
// visibility and a the ambient fraction; unlit pixels get L = E / pi * albedo.
//
// Bindings: 0 ShadingUniforms, 1 G-buffer normal, 2 G-buffer albedo, 3 G-buffer ids, 4 depth,
// 5 materials, 6 RGB-to-spectrum coefficient table, 7 its sampler, 8 spectral radiance.
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
    // Image width and height, coefficient table resolution.
    uint4 size;
    // Sun spectral irradiance at normal incidence per band (W m^-2 nm^-1), four bands per float4.
    float4 irradiance[16];
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

// Radiance factors of the active model for three bands at once: the bands' albedos go in the RGB
// slots of BrdfInput, which models treat independently.
float3 radianceFactors(float3 n, float3 l, float3 v, float3 albedo, BrdfParams params) {
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
    var coefficients = float3(0.0);
    if (scale > 0.0) {
        let resolution = float(sh.size.z);
        let colour = modulation / scale;
        let uv = (colour.xy * (resolution - 1.0) + 0.5) / resolution;
        let layer = colour.z * (resolution - 1.0);
        let lower = floor(layer);
        let upper = min(lower + 1.0, resolution - 1.0);
        let a = sigmoidTable.SampleLevel(tableSampler, float3(uv, lower), 0).xyz;
        let b = sigmoidTable.SampleLevel(tableSampler, float3(uv, upper), 0).xyz;
        coefficients = lerp(a, b, layer - lower);
    }
    let v = normalize(sh.eye.xyz - world);
    let l = sh.sun.xyz;
    let ambient = sh.sun.w;
    let bands = uint(sh.range.z);
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
        if ((ids.w & kUnlit) != 0) {
            response = bandAlbedo;
        } else {
            let factors = radianceFactors(n, l, v, bandAlbedo, material.params);
            response = ambient * bandAlbedo + (1.0 - ambient) * factors * visibility;
        }
        let radiance = bandIrradiance / PI * response;
        for (uint k = 0; k < 3 && first + k < bands; ++k) {
            spectral[int3(id.xy, first + k)] = radiance[k];
        }
    }
}
