// Lambert: I/F = A cos(i), with the albedo input A as the Lambert albedo.
fn brdf_radiance_factor(s: BrdfInput) -> vec3<f32> {
    return s.albedo * s.mu0;
}
