// Terrain G-buffer pass: the terrain's normals, shadows, albedo and ids for spectral shading.
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
GBuffer fragmentMain(VertexOutput in) {
    let detail = sampleDetail(detailCoords(in.world.xy));

    // Shadow lookups use the terrain normal; shading adds the detail normal's slope.
    let slope = terrainSlope(in.world);
    let normal = slopeNormal(slope);
    let shadingNormal = slopeNormal(slope + detail.slope);
    let range = max(t.heightRange.y - t.heightRange.x, 1e-6);
    let fraction = (in.height - t.heightRange.x) / range;

    var modulation = detail.albedo;
    var flags = 0u;
    if (t.dims.z == 1) {
        modulation = colormap(fraction);
        flags = kUnlit;
    } else if (t.dims.z == 2) {
        modulation = modulation * colormap(fraction);
    }
    modulation = cascadeTint(modulation, in.world);
    if (t.dims.w == 1) {
        let levelTints = float3[6](float3(1.0, 0.6, 0.6), float3(0.6, 1.0, 0.6),
                                   float3(0.6, 0.7, 1.0), float3(1.0, 1.0, 0.55),
                                   float3(1.0, 0.6, 1.0), float3(0.55, 1.0, 1.0));
        modulation = modulation * levelTints[in.level % 6];
    }
    GBuffer out;
    out.normal = float4(shadingNormal, flags == kUnlit ? 1.0
                                                       : sunVisibility(in.world, normal,
                                                                       in.position.xy));
    let scale = flags == kUnlit ? 1.0 : surfaceAlbedoScale(in.world);
    out.albedo = float4(scale, wireframe(modulation, in.barycentric));
    out.ids = uint4(t.ids.x, t.ids.y, 0, flags);
    return out;
}
