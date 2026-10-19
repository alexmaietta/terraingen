// Adds, to the ground under each lattice point of each crater's datum footprint on a body, the
// relief of the craters before it that can reach it (KernelBodyDatumRelief). A row is one crater,
// a column one lattice point; points with no data, or outside the footprint, hold NaN and are left
// as they are. The host side is FitBodyDatumsOnGpu (terraingen/layers/craters/shaping/apply.cpp).
#include "terraingen/gpu/kernel.slang"
#include "terraingen/layers/kernels/craters.h"

[[vk::binding(3, 0)]] StructuredBuffer<terraingen::CraterBodyDatumKernel> kernels;
[[vk::binding(4, 0)]] StructuredBuffer<terraingen::CraterRecord> records;
[[vk::binding(5, 0)]] StructuredBuffer<float> data;
[[vk::binding(6, 0)]] StructuredBuffer<terraingen::CraterFrame> frames;
[[vk::binding(7, 0)]] StructuredBuffer<float> bounds;
// Per row: its first entry and its entry count.
[[vk::binding(8, 0)]] StructuredBuffer<uint> lists;
// Record indices, ascending within each row.
[[vk::binding(9, 0)]] StructuredBuffer<uint> entries;
// The strip's lattice point positions as x, y, z triples.
[[vk::binding(10, 0)]] StructuredBuffer<float> positions;

// After every buffer (terraingen/gpu/kernel.slang).
[[vk::push_constant]] ConstantBuffer<Params> params;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!terraingen::InStrip(params, id)) {
        return;
    }
    const uint at = id.y * params.width + id.x;
    const float height = heights[at];
    if (terraingen::IsNoData(params, height)) {
        return;
    }
    const uint row = params.firstRow + id.y;
    heights[at] = height + terraingen::KernelBodyDatumRelief(
                               kernels[0].shape, data, records, frames, bounds, entries,
                               lists[2 * row], lists[2 * row + 1], positions[3 * at],
                               positions[3 * at + 1], positions[3 * at + 2]);
}
