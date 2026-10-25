// SPDX-License-Identifier: BSD-3-Clause

// Evaluates the shared noise kernels on the GPU for comparison with the C++ build of the same
// headers: per sample, every 2D basis, Perlin, simplex, value and Worley 3D noise, the Voronoi
// cell values, each fractal of a block and the whole stack, and one Philox block.
#include "terraingen/noise/fractals.h"
#include "terraingen/random/philox.h"

[[vk::binding(0, 0)]] StructuredBuffer<float> points;
[[vk::binding(1, 0)]] StructuredBuffer<terraingen::FractalStack> stacks;
[[vk::binding(2, 0)]] RWStructuredBuffer<float> values;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> words;

struct Params {
    uint count;
    uint seed;
};
[[vk::push_constant]] ConstantBuffer<Params> params;

static const uint kValues = 22u;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint i = id.x;
    if (i >= params.count) {
        return;
    }
    const float x = points[3u * i + 0u];
    const float y = points[3u * i + 1u];
    const float z = points[3u * i + 2u];
    const uint seed = params.seed;
    const uint base = kValues * i;
    values[base + 0u] = terraingen::PerlinNoise2(x, y, seed);
    values[base + 1u] = terraingen::SimplexNoise2(x, y, seed);
    values[base + 2u] = terraingen::ValueNoise2(x, y, seed);
    const terraingen::WorleyDistances worley = terraingen::Worley2(x, y, seed);
    values[base + 3u] = worley.f1;
    values[base + 4u] = worley.f2;
    values[base + 5u] = terraingen::PerlinNoise3(x, y, z, seed);
    values[base + 6u] = terraingen::BasisNoise3(terraingen::NoiseBasis::Value,
                                                terraingen::SplitLattice3(x, y, z), seed);
    const terraingen::FractalStack block = stacks[0];
    for (uint j = 0u; j < terraingen::kMaxFractals; ++j) {
        values[base + 7u + j] = terraingen::EvaluateFractal(100.0f * x, 100.0f * y,
                                                            block.fractals[j]);
    }
    values[base + 15u] = terraingen::EvaluateFractals(100.0f * x, 100.0f * y, block);
    values[base + 16u] = terraingen::HashToUnitFloat(terraingen::PcgHash(asuint(x)));
    values[base + 17u] = worley.value;
    const terraingen::WorleyDistances worley3 = terraingen::Worley3(x, y, z, seed);
    values[base + 18u] = worley3.f1;
    values[base + 19u] = worley3.f2;
    values[base + 20u] = worley3.value;
    values[base + 21u] = terraingen::SimplexNoise3(x, y, z, seed);
    terraingen::U32x4 counter = {i, asuint(x), asuint(y), seed};
    terraingen::U32x2 key = {seed, ~i};
    const terraingen::U32x4 block4 = terraingen::Philox4x32(counter, key);
    words[4u * i + 0u] = block4.x;
    words[4u * i + 1u] = block4.y;
    words[4u * i + 2u] = block4.z;
    words[4u * i + 3u] = block4.w;
}
