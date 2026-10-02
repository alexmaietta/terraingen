// Terrain colour pass. The host composes it with a reflectance model by prepending
//     import <model module>;
//     typealias ActiveBrdf = <model type>;
import brdf;
import terrain;
import surface;

struct VertexOutput {
    float4 position : SV_Position;
    float3 world : WORLD;
    float height : HEIGHT;
    float3 barycentric : BARYCENTRIC;
    nointerpolation uint level : LEVEL;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID) {
    let barycentric = float3[6](float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1), float3(1, 0, 0),
                                float3(0, 1, 0), float3(0, 0, 1));
    let v = gridVertex(vertexIndex, instanceIndex);
    VertexOutput out;
    out.position = mul(u.viewProj, float4(v.world, 1.0));
    out.world = v.world;
    out.height = v.height;
    out.barycentric = barycentric[v.corner];
    out.level = v.level;
    return out;
}

float3 colormap(float t) {
    let stops = float3[5](float3(0.16, 0.18, 0.30), float3(0.20, 0.42, 0.52),
                          float3(0.56, 0.62, 0.44), float3(0.80, 0.72, 0.52),
                          float3(0.96, 0.95, 0.92));
    let x = clamp(t, 0.0, 1.0) * 4.0;
    let i = uint(min(floor(x), 3.0));
    return lerp(stops[i], stops[i + 1], x - float(i));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput in) : SV_Target {
    let detail = sampleDetail(detailCoords(in.world.xy));

    // Shadow lookups use the terrain normal; shading adds the detail normal's slope.
    let slope = terrainSlope(in.world);
    let normal = slopeNormal(slope);
    let shadingNormal = slopeNormal(slope + detail.slope);
    let visibility = sunVisibility(in.world, normal, in.position.xy);
    let albedo = surfaceAlbedo(in.world) * detail.albedo;
    let toEye = normalize(u.eye.xyz - in.world);
    let lit = reflected<ActiveBrdf>(shadingNormal, u.sun.xyz, toEye, albedo, surface.brdf) *
              visibility * u.eye.w;
    let shade = u.sun.w * albedo + (1.0 - u.sun.w) * lit;
    let range = max(t.heightRange.y - t.heightRange.x, 1e-6);
    let fraction = (in.height - t.heightRange.x) / range;

    var color = shade;
    if (t.dims.z == 1) {
        color = colormap(fraction);
    } else if (t.dims.z == 2) {
        color = colormap(fraction) * shade;
    }
    color = cascadeTint(color, in.world);

    if (t.dims.w == 1) {
        let levelTints = float3[6](float3(1.0, 0.6, 0.6), float3(0.6, 1.0, 0.6),
                                   float3(0.6, 0.7, 1.0), float3(1.0, 1.0, 0.55),
                                   float3(1.0, 0.6, 1.0), float3(0.55, 1.0, 1.0));
        color = color * levelTints[in.level % 6];
    }
    return float4(wireframe(color, in.barycentric), 1.0);
}
