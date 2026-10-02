import brdf;

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
float3 hapkeH(float3 w, float x) {
    let gamma = sqrt(max(1.0 - w, float3(0.0)));
    let r0 = (1.0 - gamma) / (1.0 + gamma);
    let xs = max(x, 1e-6);
    let logTerm = log((1.0 + xs) / xs);
    return 1.0 / max(1.0 - w * xs * (r0 + 0.5 * (1.0 - 2.0 * r0 * xs) * logTerm), float3(1e-6));
}

// Porosity coefficient K (Hapke 2008) from the filling factor phi.
float hapkePorosityK(float phi) {
    let q = 1.209 * pow(clamp(phi, 1e-6, 0.74), 2.0 / 3.0);
    return -log(1.0 - q) / q;
}

struct HapkeRoughness {
    float mu0e;
    float mue;
    float shadowing;
}

// Hapke 1984 macroscopic roughness: effective cosines and shadowing function S for mean slope
// angle thetaBar.
HapkeRoughness hapkeRoughness(BrdfInput s, float thetaBar) {
    HapkeRoughness r;
    if (thetaBar < 1e-4) {
        r.mu0e = s.mu0;
        r.mue = s.mu;
        r.shadowing = 1.0;
        return r;
    }
    let tanT = tan(thetaBar);
    let chi = 1.0 / sqrt(1.0 + PI * tanT * tanT);
    let i = max(acos(clamp(s.mu0, -1.0, 1.0)), 1e-4);
    let e = max(acos(clamp(s.mu, -1.0, 1.0)), 1e-4);
    let psi = min(acos(clamp(s.cosPsi, -1.0, 1.0)), PI - 1e-4);
    let f = exp(-2.0 * tan(0.5 * psi));
    let sin2HalfPsi = 0.5 * (1.0 - cos(psi));

    let e1i = exp(-2.0 / (PI * tanT * tan(i)));
    let e1e = exp(-2.0 / (PI * tanT * tan(e)));
    let e2i = exp(-1.0 / (PI * tanT * tanT * tan(i) * tan(i)));
    let e2e = exp(-1.0 / (PI * tanT * tanT * tan(e) * tan(e)));
    let etaI = chi * (cos(i) + sin(i) * tanT * e2i / (2.0 - e1i));
    let etaE = chi * (cos(e) + sin(e) * tanT * e2e / (2.0 - e1e));

    if (i <= e) {
        let d = 2.0 - e1e - psi / PI * e1i;
        r.mu0e = chi * (cos(i) + sin(i) * tanT * (cos(psi) * e2e + sin2HalfPsi * e2i) / d);
        r.mue = chi * (cos(e) + sin(e) * tanT * (e2e - sin2HalfPsi * e2i) / d);
        r.shadowing = r.mue / etaE * s.mu0 / etaI * chi / (1.0 - f + f * chi * s.mu0 / etaI);
    } else {
        let d = 2.0 - e1i - psi / PI * e1e;
        r.mu0e = chi * (cos(i) + sin(i) * tanT * (e2i - sin2HalfPsi * e2e) / d);
        r.mue = chi * (cos(e) + sin(e) * tanT * (cos(psi) * e2i + sin2HalfPsi * e2e) / d);
        r.shadowing = r.mue / etaE * s.mu0 / etaI * chi / (1.0 - f + f * chi * s.mu / etaE);
    }
    return r;
}

public struct Hapke : IBrdf {
    public float3 radianceFactor(BrdfInput s, BrdfParams p) {
        let w = s.albedo;
        let g = phaseAngle(s);
        let k = hapkePorosityK(p.get(5));
        let rough = hapkeRoughness(s, radians(p.get(4)));
        let mu0e = max(rough.mu0e, 1e-6);
        let mue = max(rough.mue, 1e-6);

        let phase = doubleHenyeyGreenstein(p.get(0), p.get(1), s.cosG);
        let shoe = p.get(2) / (1.0 + tan(0.5 * g) / max(p.get(3), 1e-6));
        let multiple = hapkeH(w, mu0e / k) * hapkeH(w, mue / k) - 1.0;
        return k * 0.25 * w * mu0e / (mu0e + mue) * (phase * (1.0 + shoe) + multiple) *
               rough.shadowing;
    }
}
