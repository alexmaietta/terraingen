// Evaluates the shared hash kernels on the GPU for comparison with the C++ build of the same header.
#include "terraingen/noise/kernels/hash.h"

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> outputs;

struct Params {
    uint count;
};
[[vk::push_constant]] ConstantBuffer<Params> params;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint i = id.x;
    if (i >= params.count) {
        return;
    }
    terraingen::UVec3 v;
    v.x = i;
    v.y = i * 2654435761u;
    v.z = ~i;
    const terraingen::UVec3 h = terraingen::Pcg3dHash(v);
    outputs[5u * i + 0u] = terraingen::PcgHash(i * 7919u);
    outputs[5u * i + 1u] = h.x;
    outputs[5u * i + 2u] = h.y;
    outputs[5u * i + 3u] = h.z;
    outputs[5u * i + 4u] = asuint(terraingen::HashToUnitFloat(h.x ^ h.z));
}
