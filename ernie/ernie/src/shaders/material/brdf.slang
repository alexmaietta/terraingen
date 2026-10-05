// Shared inputs and helpers for reflectance models. A model is a struct implementing IBrdf whose
// radianceFactor returns the radiance factor I/F (pi times the bidirectional reflectance) for unit
// incident irradiance.
//
// Every backend evaluates models through the same helpers: brdfInputOf turns world or local
// directions into a BrdfInput, evaluateBrdf gives the cosine-weighted BRDF a light sample or path
// vertex multiplies incident radiance by, and sampleBrdf and brdfPdf importance sample incident
// directions for ray and path tracers.

public static const float PI = 3.14159265358979;

// Geometry of one evaluation, following Hapke's notation: i incidence angle, e emission angle,
// g phase angle, psi azimuth between the planes of incidence and emission (0 when the light and the
// viewer are on the same side of the normal). Hosts only evaluate models with mu0 and mu > 0.
public struct BrdfInput {
    public float mu0;
    public float mu;
    public float cosG;
    public float cosPsi;
    // Albedo input; each model documents how it uses it (e.g. Hapke's single-scattering albedo w).
    public float3 albedo;
}

// Up to 16 model parameters in the order the model declares them.
public struct BrdfParams {
    public float4 values[4];

    public float get(uint i) {
        return values[i / 4][i % 4];
    }
}

public interface IBrdf {
    float3 radianceFactor(BrdfInput s, BrdfParams p);
}

// The geometry of unit directions towards the light l and the viewer v about the unit normal n,
// any frame, with the given albedo input. mu0 <= 0 means the light is below the horizon, where
// models are not evaluated; mu is clamped to at least 1e-3 so views grazing or just below the
// horizon (as with interpolated shading normals) stay finite.
public BrdfInput brdfInputOf(float3 n, float3 l, float3 v, float3 albedo) {
    let mu0 = dot(n, l);
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
    return input;
}

// The BRDF times cos(i), f_r mu0 = radianceFactor / pi: the radiance reflected towards v per unit
// radiance arriving from l per steradian. Zero with the light below the horizon; never negative.
public float3 evaluateBrdf<B : IBrdf>(B model, float3 n, float3 l, float3 v, float3 albedo,
                                       BrdfParams p) {
    let input = brdfInputOf(n, l, v, albedo);
    if (input.mu0 <= 0.0) {
        return float3(0.0);
    }
    return max(model.radianceFactor(input, p), float3(0.0)) / PI;
}

// A sampled incident direction: `direction` towards the light (unit, above the horizon), its
// probability density per steradian, and `weight`, evaluateBrdf over the density, by which the
// radiance arriving along the direction scales into the radiance reflected towards the viewer.
public struct BrdfSample {
    public float3 direction;
    public float pdf;
    public float3 weight;
}

// A right-handed orthonormal basis (t, b, n) around the unit vector n, without branches on the
// normal's direction (Duff et al., "Building an Orthonormal Basis, Revisited", JCGT 6(1), 2017).
public void orthonormalBasis(float3 n, out float3 t, out float3 b) {
    let s = n.z >= 0.0 ? 1.0 : -1.0;
    let a = -1.0 / (s + n.z);
    let c = n.x * n.y * a;
    t = float3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}

// A cosine-distributed direction about the unit normal n for uniform u in [0, 1)^2, by Malley's
// method: a concentric disk sample (Shirley and Chiu 1997) lifted onto the hemisphere.
public float3 cosineHemisphere(float3 n, float2 u) {
    let d = 2.0 * u - 1.0;
    var r = 0.0;
    var phi = 0.0;
    if (d.x != 0.0 || d.y != 0.0) {
        if (abs(d.x) > abs(d.y)) {
            r = d.x;
            phi = (PI / 4.0) * (d.y / d.x);
        } else {
            r = d.y;
            phi = (PI / 2.0) - (PI / 4.0) * (d.x / d.y);
        }
    }
    let x = r * cos(phi);
    let y = r * sin(phi);
    let z = sqrt(max(0.0, 1.0 - x * x - y * y));
    float3 t;
    float3 b;
    orthonormalBasis(n, t, b);
    return normalize(x * t + y * b + z * n);
}

// The density per steradian with which sampleBrdf picks the incident direction l: cosine
// weighted, cos(i) / pi above the horizon and zero below. Models share it, since every built-in one
// is dominated by the cos(i) factor of its radiance factor; MIS weights use it with light sampling.
public float brdfPdf(float3 n, float3 l) {
    return max(dot(n, l), 0.0) / PI;
}

// Samples an incident direction for the viewer direction v with brdfPdf, from uniform u in
// [0, 1)^2. The weight is radianceFactor / mu0, exactly the albedo for Lambert.
public BrdfSample sampleBrdf<B : IBrdf>(B model, float3 n, float3 v, float3 albedo, BrdfParams p,
                                        float2 u) {
    BrdfSample sample;
    sample.direction = cosineHemisphere(n, u);
    sample.pdf = brdfPdf(n, sample.direction);
    sample.weight = float3(0.0);
    if (sample.pdf > 0.0) {
        sample.weight = evaluateBrdf(model, n, sample.direction, v, albedo, p) / sample.pdf;
    }
    return sample;
}

public float phaseAngle(BrdfInput s) {
    return acos(clamp(s.cosG, -1.0, 1.0));
}

// Single-lobe Henyey-Greenstein phase function in Hapke's sign convention: xi < 0 backscatters.
// Normalised so its average over the sphere is 1.
public float henyeyGreenstein(float xi, float cosG) {
    let xi2 = xi * xi;
    return (1.0 - xi2) / pow(max(1.0 + 2.0 * xi * cosG + xi2, 1e-6), 1.5);
}

// Two-parameter double Henyey-Greenstein phase function (Hapke 2012): lobe width b in [0, 1), and c
// in [-1, 1] weighting the backscattering lobe ((1 + c) / 2) against the forward lobe.
public float doubleHenyeyGreenstein(float b, float c, float cosG) {
    let b2 = b * b;
    let back = (1.0 - b2) / pow(max(1.0 - 2.0 * b * cosG + b2, 1e-6), 1.5);
    let forward = (1.0 - b2) / pow(max(1.0 + 2.0 * b * cosG + b2, 1e-6), 1.5);
    return 0.5 * (1.0 + c) * back + 0.5 * (1.0 - c) * forward;
}
