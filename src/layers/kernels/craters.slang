// SPDX-License-Identifier: BSD-3-Clause

// Shapes the craters of CraterShapeKernel into a strip of grid rows. Each cell holding data runs
// ApplyCraterKernel for the craters of its tile, in order, at its offset from each crater's center:
// the difference of planar positions, or on a body its offset in the crater's frame
// (KernelFrameOffset). The host side is ShapeTilesOnGpu
// (terraingen/layers/craters/shaping/apply.cpp).
#include "gpu/kernel.slang"
#include "terraingen/layers/kernels/craters.h"

[[vk::binding(3, 0)]] StructuredBuffer<terraingen::CraterShapeKernel> kernels;
[[vk::binding(4, 0)]] StructuredBuffer<terraingen::CraterRecord> records;
[[vk::binding(5, 0)]] StructuredBuffer<float> data;
// Tiles across, cells per tile side, 1 on a body, then the first entry and the entry count of each
// tile.
[[vk::binding(6, 0)]] StructuredBuffer<uint> tiles;
[[vk::binding(7, 0)]] StructuredBuffer<uint> entries;
// On a body: each crater's frame, and the strip's cell positions as x, y, z triples.
[[vk::binding(8, 0)]] StructuredBuffer<terraingen::CraterFrame> frames;
[[vk::binding(9, 0)]] StructuredBuffer<float> positions;

// After every buffer (terraingen/gpu/kernel.slang).
[[vk::push_constant]] ConstantBuffer<Params> params;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!terraingen::InStrip(params, id)) {
        return;
    }
    const uint i = id.y * params.width + id.x;
    float height = heights[i];
    if (terraingen::IsNoData(params, height)) {
        return;
    }
    const uint side = tiles[1];
    const uint tile = ((params.firstRow + id.y) / side) * tiles[0] + id.x / side;
    const uint first = tiles[3 + 2 * tile];
    const uint count = tiles[4 + 2 * tile];
    if (tiles[2] == 0) {
        const float x = xs[id.x];
        const float y = ys[id.y];
        for (uint k = first; k < first + count; ++k) {
            const terraingen::CraterRecord crater = records[entries[k]];
            height = terraingen::ApplyCraterKernel(kernels[0], data, crater, height, x - crater.x,
                                                   y - crater.y);
        }
    } else {
        const float px = positions[3 * i];
        const float py = positions[3 * i + 1];
        const float pz = positions[3 * i + 2];
        for (uint k = first; k < first + count; ++k) {
            const uint n = entries[k];
            const terraingen::KernelOffset at = terraingen::KernelFrameOffset(frames[n], px, py, pz);
            height = terraingen::ApplyCraterKernel(kernels[0], data, records[n], height, at.dx,
                                                   at.dy);
        }
    }
    heights[i] = height;
}
