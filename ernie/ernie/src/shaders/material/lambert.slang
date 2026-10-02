import brdf;

// Lambert: I/F = A cos(i), with the albedo input A as the Lambert albedo.
public struct Lambert : IBrdf {
    public float3 radianceFactor(BrdfInput s, BrdfParams p) {
        return s.albedo * s.mu0;
    }
}
