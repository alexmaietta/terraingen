// Shared inputs and helpers for reflectance models. A model defines
//     fn brdf_radiance_factor(s: BrdfInput) -> vec3<f32>
// returning the radiance factor I/F (pi times the bidirectional reflectance) for unit incident
// irradiance, and reads its parameters with brdf_param(i), which the host shader provides.

const PI: f32 = 3.14159265358979;

// Geometry of one evaluation, following Hapke's notation: i incidence angle, e emission angle,
// g phase angle, psi azimuth between the planes of incidence and emission (0 when the light and the
// viewer are on the same side of the normal). The host only calls a model with mu0 and mu > 0.
struct BrdfInput {
    mu0: f32,
    mu: f32,
    cos_g: f32,
    cos_psi: f32,
    // Albedo input; each model documents how it uses it (e.g. Hapke's single-scattering albedo w).
    albedo: vec3<f32>,
}

fn brdf_phase_angle(s: BrdfInput) -> f32 {
    return acos(clamp(s.cos_g, -1.0, 1.0));
}

// Single-lobe Henyey-Greenstein phase function in Hapke's sign convention: xi < 0 backscatters.
// Normalised so its average over the sphere is 1.
fn henyey_greenstein(xi: f32, cos_g: f32) -> f32 {
    let xi2 = xi * xi;
    return (1.0 - xi2) / pow(max(1.0 + 2.0 * xi * cos_g + xi2, 1e-6), 1.5);
}

// Two-parameter double Henyey-Greenstein phase function (Hapke 2012): lobe width b in
// [0, 1), and c in [-1, 1] weighting the backscattering lobe ((1 + c) / 2) against the forward lobe.
fn double_henyey_greenstein(b: f32, c: f32, cos_g: f32) -> f32 {
    let b2 = b * b;
    let back = (1.0 - b2) / pow(max(1.0 - 2.0 * b * cos_g + b2, 1e-6), 1.5);
    let forward = (1.0 - b2) / pow(max(1.0 + 2.0 * b * cos_g + b2, 1e-6), 1.5);
    return 0.5 * (1.0 + c) * back + 0.5 * (1.0 - c) * forward;
}
