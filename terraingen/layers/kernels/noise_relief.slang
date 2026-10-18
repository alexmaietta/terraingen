// Adds NoiseReliefKernel to a strip of grid rows. The host side is RunKernel
// (terraingen/gpu/kernel.h).
#include "terraingen/gpu/kernel.slang"
#include "terraingen/layers/kernels/noise_relief.h"

[[vk::binding(3, 0)]] StructuredBuffer<terraingen::NoiseReliefKernel> kernels;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!terraingen::InStrip(id)) {
        return;
    }
    const uint i = id.y * params.width + id.x;
    const float height = heights[i];
    if (terraingen::IsNoData(height)) {
        return;
    }
    terraingen::CellPosition position = {xs[id.x], ys[id.y]};
    heights[i] = terraingen::ApplyCell(kernels[0], height, position);
}
