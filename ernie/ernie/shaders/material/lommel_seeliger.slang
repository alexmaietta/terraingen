import brdf;

// Lommel-Seeliger single scattering: I/F = (w / 4) mu0 / (mu0 + mu) p(g), with the albedo input as
// the single-scattering albedo w and a Henyey-Greenstein phase function p (Hapke 2012, without
// multiple scattering or opposition effects).
// Parameters: 0 Henyey-Greenstein asymmetry xi.
public struct LommelSeeliger : IBrdf {
    public float3 radianceFactor(BrdfInput s, BrdfParams p) {
        let phase = henyeyGreenstein(p.get(0), s.cosG);
        return 0.25 * s.albedo * s.mu0 / (s.mu0 + s.mu) * phase;
    }
}
