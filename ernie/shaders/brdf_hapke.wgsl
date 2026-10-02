// Hapke model as parameterised for the LROC WAC Hapke maps (Sato et al. 2014): the Hapke 2012
// form with porosity K, shadow-hiding opposition effect, double Henyey-Greenstein phase function,
// isotropic multiple scattering through the Hapke 2002 H-function approximation, and Hapke 1984
// macroscopic roughness. No coherent backscatter opposition effect.
//
//   I/F = K (w / 4) mu0e / (mu0e + mue) [p(g) (1 + B_S0 B_S(g)) + H(mu0e / K) H(mue / K) - 1]
//         S(i, e, psi)
//
// The albedo input is the single-scattering albedo w.
// Parameters: 0 b, 1 c (double Henyey-Greenstein), 2 B_S0, 3 h_S, 4 theta-bar in degrees,
// 5 filling factor phi (volume fraction occupied by particles; K -> 1 as phi -> 0).

// Hapke 2002 approximation to Chandrasekhar's H-function for isotropic scatterers.
fn hapke_h(w: vec3<f32>, x: f32) -> vec3<f32> {
    let gamma = sqrt(max(1.0 - w, vec3<f32>(0.0)));
    let r0 = (1.0 - gamma) / (1.0 + gamma);
    let xs = max(x, 1e-6);
    let log_term = log((1.0 + xs) / xs);
    return 1.0 / max(1.0 - w * xs * (r0 + 0.5 * (1.0 - 2.0 * r0 * xs) * log_term), vec3<f32>(1e-6));
}

// Porosity coefficient K (Hapke 2008) from the filling factor phi.
fn hapke_porosity_k(phi: f32) -> f32 {
    let p = 1.209 * pow(clamp(phi, 1e-6, 0.74), 2.0 / 3.0);
    return -log(1.0 - p) / p;
}

struct HapkeRoughness {
    mu0e: f32,
    mue: f32,
    shadowing: f32,
}

// Hapke 1984 macroscopic roughness: effective cosines and shadowing function S for mean slope
// angle theta_bar.
fn hapke_roughness(s: BrdfInput, theta_bar: f32) -> HapkeRoughness {
    var r: HapkeRoughness;
    if (theta_bar < 1e-4) {
        r.mu0e = s.mu0;
        r.mue = s.mu;
        r.shadowing = 1.0;
        return r;
    }
    let tan_t = tan(theta_bar);
    let chi = 1.0 / sqrt(1.0 + PI * tan_t * tan_t);
    let i = max(acos(clamp(s.mu0, -1.0, 1.0)), 1e-4);
    let e = max(acos(clamp(s.mu, -1.0, 1.0)), 1e-4);
    let psi = min(acos(clamp(s.cos_psi, -1.0, 1.0)), PI - 1e-4);
    let f = exp(-2.0 * tan(0.5 * psi));
    let sin2_half_psi = 0.5 * (1.0 - cos(psi));

    let e1_i = exp(-2.0 / (PI * tan_t * tan(i)));
    let e1_e = exp(-2.0 / (PI * tan_t * tan(e)));
    let e2_i = exp(-1.0 / (PI * tan_t * tan_t * tan(i) * tan(i)));
    let e2_e = exp(-1.0 / (PI * tan_t * tan_t * tan(e) * tan(e)));
    let eta_i = chi * (cos(i) + sin(i) * tan_t * e2_i / (2.0 - e1_i));
    let eta_e = chi * (cos(e) + sin(e) * tan_t * e2_e / (2.0 - e1_e));

    if (i <= e) {
        let d = 2.0 - e1_e - psi / PI * e1_i;
        r.mu0e = chi * (cos(i) + sin(i) * tan_t * (cos(psi) * e2_e + sin2_half_psi * e2_i) / d);
        r.mue = chi * (cos(e) + sin(e) * tan_t * (e2_e - sin2_half_psi * e2_i) / d);
        r.shadowing = r.mue / eta_e * s.mu0 / eta_i * chi /
                      (1.0 - f + f * chi * s.mu0 / eta_i);
    } else {
        let d = 2.0 - e1_i - psi / PI * e1_e;
        r.mu0e = chi * (cos(i) + sin(i) * tan_t * (e2_i - sin2_half_psi * e2_e) / d);
        r.mue = chi * (cos(e) + sin(e) * tan_t * (cos(psi) * e2_i + sin2_half_psi * e2_e) / d);
        r.shadowing = r.mue / eta_e * s.mu0 / eta_i * chi /
                      (1.0 - f + f * chi * s.mu / eta_e);
    }
    return r;
}

fn brdf_radiance_factor(s: BrdfInput) -> vec3<f32> {
    let w = s.albedo;
    let g = brdf_phase_angle(s);
    let k = hapke_porosity_k(brdf_param(5u));
    let rough = hapke_roughness(s, radians(brdf_param(4u)));
    let mu0e = max(rough.mu0e, 1e-6);
    let mue = max(rough.mue, 1e-6);

    let phase = double_henyey_greenstein(brdf_param(0u), brdf_param(1u), s.cos_g);
    let shoe = brdf_param(2u) / (1.0 + tan(0.5 * g) / max(brdf_param(3u), 1e-6));
    let multiple = hapke_h(w, mu0e / k) * hapke_h(w, mue / k) - 1.0;
    return k * 0.25 * w * mu0e / (mu0e + mue) * (phase * (1.0 + shoe) + multiple) *
           rough.shadowing;
}
