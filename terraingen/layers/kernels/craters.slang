// Shapes the craters of CraterShapeKernel into a strip of grid rows. Each cell holding data runs
// ApplyCraterKernel for the craters of its tile, in order, at its offset from each crater's centre.
// The host side is ShapeTilesOnGpu (terraingen/layers/craters/shaping/apply.cpp).
#include "terraingen/gpu/kernel.slang"
#include "terraingen/layers/kernels/craters.h"

[[vk::binding(3, 0)]] StructuredBuffer<terraingen::CraterShapeKernel> kernels;
[[vk::binding(4, 0)]] StructuredBuffer<terraingen::CraterRecord> records;
[[vk::binding(5, 0)]] StructuredBuffer<float> data;
// Tiles across, cells per tile side, then the first entry and the entry count of each tile.
[[vk::binding(6, 0)]] StructuredBuffer<uint> tiles;
[[vk::binding(7, 0)]] StructuredBuffer<uint> entries;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!terraingen::InStrip(id)) {
        return;
    }
    const uint i = id.y * params.width + id.x;
    float height = heights[i];
    if (terraingen::IsNoData(height)) {
        return;
    }
    const uint side = tiles[1];
    const uint tile = ((params.firstRow + id.y) / side) * tiles[0] + id.x / side;
    const uint first = tiles[2 + 2 * tile];
    const uint count = tiles[3 + 2 * tile];
    const float x = xs[id.x];
    const float y = ys[id.y];
    for (uint k = first; k < first + count; ++k) {
        const terraingen::CraterRecord crater = records[entries[k]];
        height = terraingen::ApplyCraterKernel(kernels[0], data, crater, height, x - crater.x,
                                               y - crater.y);
    }
    heights[i] = height;
}
