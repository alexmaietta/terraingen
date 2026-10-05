// Ground plane G-buffer pass: an infinite horizontal plane at z = height, drawn with a full-screen
// triangle. Each fragment intersects its camera ray with the plane and writes the hit's depth, so
// other parts sort against it; rays that miss (or hit behind the camera) are discarded, and hits
// past the far plane are pulled just inside it so the plane reaches the horizon.
//
// An optional checkerboard modulates the material's albedo, box-filtered over the pixel's
// footprint so it fades to its mean colour with distance instead of aliasing (Quilez, "Filtering
// the Checkerboard Pattern", https://iquilezles.org/articles/checkerfiltering/).
//
// Bindings: frame 0 to 4 (frame.slang), material 5 to 9 (surface.slang), 10 GroundUniforms.
import frame;
import surface;

struct GroundUniforms {
    float4x4 invViewProj;
    // Height, checker square size, checker on, unused.
    float4 plane;
    // Linear checker colours, rgb.
    float4 colour0;
    float4 colour1;
    // Image width and height in pixels.
    float4 viewport;
    // Material, prim id.
    uint4 ids;
}

[[vk::binding(10, 0)]] ConstantBuffer<GroundUniforms> g;

struct VertexOutput {
    float4 position : SV_Position;
}

struct GroundOutput {
    float4 normal : SV_Target0;
    float4 albedo : SV_Target1;
    uint4 ids : SV_Target2;
    float depth : SV_Depth;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID) {
    let uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput out;
    out.position = float4(uv * 2.0 - 1.0, 0.5, 1.0);
    return out;
}

// Box-filtered checkerboard of unit squares at `p`: 0 on one colour, 1 on the other, blending to
// 0.5 as the filter footprint grows past a square.
float filteredChecker(float2 p) {
    let w = max(abs(ddx(p)), abs(ddy(p))) + 1e-4;
    let i = 2.0 * (abs(frac((p - 0.5 * w) * 0.5) - 0.5) - abs(frac((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

[shader("fragment")]
GroundOutput fragmentMain(VertexOutput in) {
    let uv = in.position.xy / g.viewport.xy;
    let ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let nearClip = mul(g.invViewProj, float4(ndc, 0.0, 1.0));
    let farClip = mul(g.invViewProj, float4(ndc, 1.0, 1.0));
    let origin = nearClip.xyz / nearClip.w;
    let direction = farClip.xyz / farClip.w - origin;
    let height = g.plane.x;
    let valid = abs(direction.z) > 1e-12;
    let t = valid ? (height - origin.z) / direction.z : -1.0;
    let world = origin + direction * max(t, 0.0);

    // Everything that takes screen-space derivatives runs before the discard.
    let detail = sampleDetail(detailCoords(world.xy));
    var modulation = detail.albedo;
    if (g.plane.z > 0.0) {
        let blend = filteredChecker(world.xy / max(g.plane.y, 1e-6));
        modulation = modulation * lerp(g.colour0.rgb, g.colour1.rgb, blend);
    }
    if (!(t > 0.0)) {
        discard;
    }

    let clip = mul(u.viewProj, float4(world, 1.0));
    let normal = float3(0.0, 0.0, 1.0);
    GroundOutput out;
    out.normal = float4(normalize(float3(-detail.slope, 1.0)), sunVisibility(world, normal,
                                                                             in.position.xy));
    out.albedo = float4(surfaceAlbedoScale(world), cascadeTint(modulation, world));
    out.ids = uint4(g.ids.x, g.ids.y, 0, 0);
    out.depth = min(clip.z / clip.w, 0.99999994);
    return out;
}
