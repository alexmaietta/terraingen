// Lunar-Lambert (McEwen 1991): I/F = A [2 L mu0 / (mu0 + mu) + (1 - L) mu0], blending
// Lommel-Seeliger and Lambert with a phase-dependent weight L(g) = 1 + a1 g + a2 g^2 + a3 g^3, g in
// degrees. The albedo input is A, the I/F at normal incidence and emission.
// Parameters: 0 a1, 1 a2, 2 a3.
fn brdf_radiance_factor(s: BrdfInput) -> vec3<f32> {
    let g = degrees(brdf_phase_angle(s));
    let l = 1.0 + g * (brdf_param(0u) + g * (brdf_param(1u) + g * brdf_param(2u)));
    return s.albedo * (2.0 * l * s.mu0 / (s.mu0 + s.mu) + (1.0 - l) * s.mu0);
}
