// SPDX-License-Identifier: BSD-3-Clause

// Shapes the craters of CraterShapeKernel into a strip of grid rows: each cell holding data runs
// ShapeCraters, or ShapeCratersOnBody on a body, over the craters of its tile, as the CPU does
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
// On a body: each crater's frame, and the strip's cells as split points.
[[vk::binding(8, 0)]] StructuredBuffer<terraingen::CraterFrame> frames;
[[vk::binding(9, 0)]] StructuredBuffer<terraingen::SplitPoint> points;
// On a plane: what xs leaves of each column's x, then what ys leaves of each row's y.
[[vk::binding(10, 0)]] StructuredBuffer<float> lows;

// After every buffer (terraingen/gpu/kernel.slang).
[[vk::push_constant]] ConstantBuffer<Params> params;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!terraingen::InStrip(params, id)) {
        return;
    }
    const uint i = id.y * params.width + id.x;
    const float height = heights[i];
    if (terraingen::IsNoData(params, height)) {
        return;
    }
    const uint row = params.firstRow + id.y;
    const uint head = terraingen::CraterTileHead(tiles, id.x, row);
    if (tiles[2] == 0) {
        terraingen::SplitPoint p;
        p.x = xs[id.x];
        p.xLow = lows[id.x];
        p.y = ys[id.y];
        p.yLow = lows[params.width + row];
        p.z = 0.0;
        p.zLow = 0.0;
        heights[i] = terraingen::ShapeCraters(kernels[0], data, records, entries, tiles[head],
                                              tiles[head + 1], height, p);
    } else {
        heights[i] = terraingen::ShapeCratersOnBody(kernels[0], data, records, frames, entries,
                                                    tiles[head], tiles[head + 1], height, points[i]);
    }
}
