// SPDX-License-Identifier: BSD-3-Clause

// Adds, to the ground under each lattice point of each crater's datum footprint, the relief of the
// craters applied before it (KernelDatumRelief). A row is one crater (`targets`), a column one
// lattice point. The host side is FitDatumsWithKernel
// (terraingen/layers/craters/shaping/datum_fit.cpp).
#include "gpu/kernel.slang"
#include "terraingen/layers/kernels/craters.h"

[[vk::binding(3, 0)]] StructuredBuffer<terraingen::CraterDatumKernel> kernels;
[[vk::binding(4, 0)]] StructuredBuffer<terraingen::CraterRecord> records;
[[vk::binding(5, 0)]] StructuredBuffer<float> data;
[[vk::binding(6, 0)]] StructuredBuffer<float> lows;
[[vk::binding(7, 0)]] StructuredBuffer<float> bandFloats;
[[vk::binding(8, 0)]] StructuredBuffer<uint> bandCells;
[[vk::binding(9, 0)]] StructuredBuffer<uint> cells;
[[vk::binding(10, 0)]] StructuredBuffer<uint> entries;
// The crater of each row.
[[vk::binding(11, 0)]] StructuredBuffer<uint> targets;

// After every buffer (terraingen/gpu/kernel.slang).
[[vk::push_constant]] ConstantBuffer<Params> params;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!terraingen::InStrip(params, id)) {
        return;
    }
    const uint at = id.y * params.width + id.x;
    heights[at] = heights[at] + terraingen::KernelDatumRelief(kernels[0], data, records, lows,
                                                              bandFloats, bandCells, cells,
                                                              entries,
                                                              targets[params.firstRow + id.y],
                                                              id.x);
}
