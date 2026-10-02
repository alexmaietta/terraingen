// Present pass: draws a frame's LdrColor (sRGB-encoded RGBA8) into a colour attachment with a
// full-screen triangle, decoding to linear first when the attachment encodes sRGB itself.

struct PresentConstants {
    // x: 1 when the attachment has an sRGB format.
    uint4 flags;
}

[[vk::binding(0, 0)]] Texture2D<float4> ldrColor;
[[vk::push_constant]] ConstantBuffer<PresentConstants> constants;

struct VertexOutput {
    float4 position : SV_Position;
}

[shader("vertex")]
VertexOutput presentVertex(uint vertexIndex : SV_VertexID) {
    let uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput out;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

float decodeSrgb(float encoded) {
    return encoded <= 0.04045 ? encoded / 12.92 : pow((encoded + 0.055) / 1.055, 2.4);
}

[shader("fragment")]
float4 presentFragment(VertexOutput in) : SV_Target {
    let colour = ldrColor.Load(int3(int2(in.position.xy), 0));
    if (constants.flags.x == 1) {
        return float4(decodeSrgb(colour.r), decodeSrgb(colour.g), decodeSrgb(colour.b), 1.0);
    }
    return float4(colour.rgb, 1.0);
}
