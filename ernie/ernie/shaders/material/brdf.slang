// Shared inputs and helpers for reflectance models. A model is a struct implementing IBrdf whose
// radianceFactor returns the radiance factor I/F (pi times the bidirectional reflectance) for unit
// incident irradiance.

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
