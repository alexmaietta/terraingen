// Adds, to the ground under each lattice point of each crater's datum footprint, the relief of the
// craters applied before it. A row is one crater (`targets`), a column one lattice point. The host
// side is FitDatumsOnGpu (terraingen/layers/craters/shaping/apply.cpp).
#include "terraingen/gpu/kernel.slang"
#include "terraingen/layers/kernels/craters.h"

[[vk::binding(3, 0)]] StructuredBuffer<terraingen::CraterDatumKernel> kernels;
[[vk::binding(4, 0)]] StructuredBuffer<terraingen::CraterRecord> records;
[[vk::binding(5, 0)]] StructuredBuffer<float> data;
// Per size band: the x and y of its grid's corner and its cell size.
[[vk::binding(6, 0)]] StructuredBuffer<float> bandFloats;
// Per size band: cells across, cells down and its first cell.
[[vk::binding(7, 0)]] StructuredBuffer<uint> bandCells;
// Per cell: its first entry and its entry count.
[[vk::binding(8, 0)]] StructuredBuffer<uint> cells;
[[vk::binding(9, 0)]] StructuredBuffer<uint> entries;
// The crater of each row.
[[vk::binding(10, 0)]] StructuredBuffer<uint> targets;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!terraingen::InStrip(id)) {
        return;
    }
    const terraingen::CraterDatumKernel kernel = kernels[0];
    const uint k = targets[params.firstRow + id.y];
    const terraingen::CraterRecord crater = records[k];
    const uint side = kernel.latticeSide;
    const float fitRadius = kernel.fitRadii * 0.5f * crater.diameter;
    const float dx = terraingen::KernelDatumOffset(id.x % side, side, fitRadius);
    const float dy = terraingen::KernelDatumOffset(id.x / side, side, fitRadius);
    if (dx * dx + dy * dy >= fitRadius * fitRadius) {
        return;
    }
    const float px = crater.x + dx;
    const float py = crater.y + dy;
    float relief = 0.0f;
    for (uint b = 0; b < kernel.bandCount; ++b) {
        const float size = bandFloats[3 * b + 2];
        const int ci = int(floor((px - bandFloats[3 * b]) / size));
        const int cj = int(floor((py - bandFloats[3 * b + 1]) / size));
        const int across = int(bandCells[3 * b]);
        const int down = int(bandCells[3 * b + 1]);
        for (int j = cj - 1; j <= cj + 1; ++j) {
            for (int i = ci - 1; i <= ci + 1; ++i) {
                if (i < 0 || j < 0 || i >= across || j >= down) {
                    continue;
                }
                const uint cell = bandCells[3 * b + 2] + uint(j * across + i);
                const uint first = cells[2 * cell];
                const uint count = cells[2 * cell + 1];
                for (uint e = first; e < first + count; ++e) {
                    const uint n = entries[e];
                    if (n >= k) {
                        break;
                    }
                    const terraingen::CraterRecord other = records[n];
                    relief += terraingen::CraterRelief(kernel.shape, data, other, px - other.x,
                                                       py - other.y);
                }
            }
        }
    }
    const uint at = id.y * params.width + id.x;
    heights[at] = heights[at] + relief;
}
