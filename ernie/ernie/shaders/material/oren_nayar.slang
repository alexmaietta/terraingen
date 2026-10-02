import brdf;

// Oren-Nayar qualitative model (Oren and Nayar 1994): Lambertian V-cavities with Gaussian slope
// distribution of standard deviation sigma. I/F = rho mu0 (A + B max(0, cos psi) sin(alpha)
// tan(beta)), alpha = max(i, e), beta = min(i, e). The albedo input is rho.
// Parameters: 0 sigma in degrees.
public struct OrenNayar : IBrdf {
    public float3 radianceFactor(BrdfInput s, BrdfParams p) {
        let sigma = radians(p.get(0));
        let sigma2 = sigma * sigma;
        let a = 1.0 - 0.5 * sigma2 / (sigma2 + 0.33);
        let b = 0.45 * sigma2 / (sigma2 + 0.09);
        let i = acos(clamp(s.mu0, -1.0, 1.0));
        let e = acos(clamp(s.mu, -1.0, 1.0));
        let alpha = max(i, e);
        let beta = min(i, e);
        return s.albedo * s.mu0 * (a + b * max(0.0, s.cosPsi) * sin(alpha) * tan(beta));
    }
}
