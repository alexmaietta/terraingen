import brdf;

// Lunar-Lambert (McEwen 1991): I/F = A [2 L mu0 / (mu0 + mu) + (1 - L) mu0], blending
// Lommel-Seeliger and Lambert with a phase-dependent weight L(g) = 1 + a1 g + a2 g^2 + a3 g^3, g in
// degrees. The albedo input is A, the I/F at normal incidence and emission.
// Parameters: 0 a1, 1 a2, 2 a3.
public struct LunarLambert : IBrdf {
    public float3 radianceFactor(BrdfInput s, BrdfParams p) {
        let g = degrees(phaseAngle(s));
        let l = 1.0 + g * (p.get(0) + g * (p.get(1) + g * p.get(2)));
        return s.albedo * (2.0 * l * s.mu0 / (s.mu0 + s.mu) + (1.0 - l) * s.mu0);
    }
}
