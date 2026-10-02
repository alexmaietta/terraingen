// Lommel-Seeliger single scattering: I/F = (w / 4) mu0 / (mu0 + mu) p(g), with the albedo input as
// the single-scattering albedo w and a Henyey-Greenstein phase function p (Hapke 2012
// without multiple scattering or opposition effects).
// Parameters: 0 Henyey-Greenstein asymmetry xi.
fn brdf_radiance_factor(s: BrdfInput) -> vec3<f32> {
    let phase = henyey_greenstein(brdf_param(0u), s.cos_g);
    return 0.25 * s.albedo * s.mu0 / (s.mu0 + s.mu) * phase;
}
