// Mesh and PointInstancer passes. Geometry is pulled from storage buffers rather than vertex
// buffers. Besides the frame (bindings 0 to 4, frame.slang) and the draw's material (5 to 9,
// surface.slang) the passes bind
//   10 points, 11 normals, 12 triangle indices, 13 draws, 14 vertex colours,
//   15 instance positions, 16 instance orientations, 17 instance scales, 18 instance ids.
// The colour pass writes the G-buffer for spectral shading (frame.slang).
import frame;
import surface;

static const uint kHasNormals = 1;
static const uint kHasColors = 2;
static const uint kInstanced = 4;
static const uint kHasOrientations = 8;
static const uint kHasScales = 16;

struct MeshDraw {
    // World transform of the mesh, or of the instancer for instanced draws.
    float4x4 model;
    // Instanced draws: the prototype's own transform, applied before the instance's.
    float4x4 prototype;
    // rgb: tint on the material's albedo where the mesh has no vertex colours, a: unused.
    float4 tint;
    // First index, first vertex, first instance id, flags.
    uint4 offsets;
    // First point in `points` (which differs from the first vertex for bound points), material,
    // prim id, and the instancer's first entry in the instance arrays.
    uint4 points;
}

[[vk::binding(10, 0)]] StructuredBuffer<float> points;
[[vk::binding(11, 0)]] StructuredBuffer<float> normals;
[[vk::binding(12, 0)]] StructuredBuffer<uint> indices;
[[vk::binding(13, 0)]] StructuredBuffer<MeshDraw> draws;
[[vk::binding(14, 0)]] StructuredBuffer<float> colors;
[[vk::binding(15, 0)]] StructuredBuffer<float> positions;
// Quaternions as (x, y, z, w), imaginary part first, as in USD's quath memory layout.
[[vk::binding(16, 0)]] StructuredBuffer<float> orientations;
[[vk::binding(17, 0)]] StructuredBuffer<float> scales;
// Per instanced draw, the instances of its prototype as indices into the instance arrays.
[[vk::binding(18, 0)]] StructuredBuffer<uint> instanceIds;

float3 load3(StructuredBuffer<float> buffer, uint i) {
    return float3(buffer[3 * i], buffer[3 * i + 1], buffer[3 * i + 2]);
}

float3 transformPoint(float4x4 m, float3 p) {
    return mul(m, float4(p, 1.0)).xyz;
}

// Normal through the upper 3x3 of `m` by its cofactor matrix, proportional to the inverse
// transpose, so non-uniform scales keep normals perpendicular; not normalised.
float3 transformNormal(float4x4 m, float3 n) {
    let c0 = float3(m[0][0], m[1][0], m[2][0]);
    let c1 = float3(m[0][1], m[1][1], m[2][1]);
    let c2 = float3(m[0][2], m[1][2], m[2][2]);
    let product = n.x * cross(c1, c2) + n.y * cross(c2, c0) + n.z * cross(c0, c1);
    return dot(c0, cross(c1, c2)) < 0.0 ? -product : product;
}

float3 rotate(float4 q, float3 v) {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

struct MeshVertex {
    float3 world;
    float3 normal;
    float3 color;
    uint corner;
    uint instance;
}

// Vertex `vertexIndex` (three per triangle) of instance `instanceIndex` of the current draw.
MeshVertex meshVertex(uint vertexIndex, uint instanceIndex) {
    let d = draws[draw.first];
    let flags = d.offsets.w;
    let index = d.offsets.y + indices[d.offsets.x + vertexIndex];
    var p = load3(points, d.points.x + indices[d.offsets.x + vertexIndex]);
    var n = (flags & kHasNormals) != 0 ? load3(normals, index) : float3(0.0);
    if ((flags & kInstanced) != 0) {
        let instance = instanceIds[d.offsets.z + instanceIndex];
        p = transformPoint(d.prototype, p);
        n = transformNormal(d.prototype, n);
        if ((flags & kHasScales) != 0) {
            let scale = load3(scales, instance);
            p = p * scale;
            n = n / scale;
        }
        if ((flags & kHasOrientations) != 0) {
            let q = float4(orientations[4 * instance], orientations[4 * instance + 1],
                           orientations[4 * instance + 2], orientations[4 * instance + 3]);
            let unit = q / max(length(q), 1e-20);
            p = rotate(unit, p);
            n = rotate(unit, n);
        }
        p = p + load3(positions, instance);
    }
    MeshVertex v;
    v.world = transformPoint(d.model, p);
    v.normal = transformNormal(d.model, n);
    v.color = (flags & kHasColors) != 0 ? load3(colors, index) : d.tint.rgb;
    v.corner = vertexIndex % 3;
    v.instance = (flags & kInstanced) != 0 ? instanceIds[d.offsets.z + instanceIndex] - d.points.w
                                           : 0u;
    return v;
}

struct VertexOutput {
    float4 position : SV_Position;
    float3 world : WORLD;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float3 barycentric : BARYCENTRIC;
    nointerpolation uint instance : INSTANCE;
}

[shader("vertex")]
VertexOutput meshVertexMain(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID) {
    let barycentric = float3[3](float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1));
    let v = meshVertex(vertexIndex, instanceIndex);
    VertexOutput out;
    out.position = mul(u.viewProj, float4(v.world, 1.0));
    out.world = v.world;
    out.normal = v.normal;
    out.color = v.color;
    out.barycentric = barycentric[v.corner];
    out.instance = v.instance;
    return out;
}

[shader("vertex")]
float4 meshShadowVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
    : SV_Position {
    return mul(draw.lightViewProj, float4(meshVertex(vertexIndex, instanceIndex).world, 1.0));
}

// Detail textures projected along the three world axes and blended by the normal (triplanar
// mapping); detail slopes perturb the normal within each projection plane (UDN blending).
//
// Reference: B. Golus, "Normal Mapping for a Triplanar Shader", 2017.
// https://bgolus.medium.com/normal-mapping-for-a-triplanar-shader-10bf39dca05a
DetailSample triplanarDetail(float3 world, inout float3 normal) {
    var w = pow(abs(normal), float3(4.0));
    w = w / max(w.x + w.y + w.z, 1e-6);
    let x = sampleDetail(detailCoords(world.yz));
    let y = sampleDetail(detailCoords(world.xz));
    let z = sampleDetail(detailCoords(world.xy));
    DetailSample d;
    d.albedo = w.x * x.albedo + w.y * y.albedo + w.z * z.albedo;
    d.slope = float2(0.0);
    let perturbation = w.x * float3(0.0, -x.slope.x, -x.slope.y) +
                       w.y * float3(-y.slope.x, 0.0, -y.slope.y) +
                       w.z * float3(-z.slope.x, -z.slope.y, 0.0);
    normal = normalize(normal + perturbation);
    return d;
}

// Meshes are two-sided: normals turn towards the viewer. Without authored normals each triangle
// is lit flat with the normal of its plane. The albedo modulation is the mesh's vertex colours or
// tint times the detail albedo; the material's albedo map scales its reflectance by world x and y.
[shader("fragment")]
GBuffer meshFragmentMain(VertexOutput in) {
    let d = draws[draw.first];
    let toEye = normalize(u.eye.xyz - in.world);
    let flat = cross(ddx(in.world), ddy(in.world));
    var normal = (d.offsets.w & kHasNormals) != 0 && dot(in.normal, in.normal) > 0.0
                     ? normalize(in.normal)
                     : normalize(flat);
    if (dot(normal, toEye) < 0.0) {
        normal = -normal;
    }
    var shadingNormal = normal;
    let detail = triplanarDetail(in.world, shadingNormal);
    let modulation = cascadeTint(in.color * detail.albedo, in.world);
    GBuffer out;
    out.normal = float4(shadingNormal, sunVisibility(in.world, normal, in.position.xy));
    out.albedo = float4(surfaceAlbedoScale(in.world), wireframe(modulation, in.barycentric));
    out.ids = uint4(d.points.y, d.points.z, in.instance, 0u);
    return out;
}
