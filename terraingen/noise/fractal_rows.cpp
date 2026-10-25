// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/fractal_rows.h"

#include <vector>

/// Compiles a function for AVX2 and for the baseline, picking one when the library loads; only
/// where the loader supports it (x86-64 Linux with GCC or Clang).
#if defined(__x86_64__) && defined(__linux__) && (defined(__GNUC__) || defined(__clang__))
#define TERRAINGEN_AVX2_CLONES __attribute__((target_clones("avx2", "default")))
#else
#define TERRAINGEN_AVX2_CLONES
#endif

namespace terraingen {

namespace {

/// Points evaluated together, so the octave's arrays stay in the first-level cache.
constexpr std::size_t kRun = 256;

/// One octave's basis noise at `count` lattice positions.
TERRAINGEN_AVX2_CLONES
void BasisRun(NoiseBasis basis, const float* lx, const float* ly, std::size_t count,
              std::uint32_t seed, float* noise) {
    if (basis == NoiseBasis::Perlin) {
        for (std::size_t i = 0; i < count; ++i) {
            noise[i] = PerlinNoise2(lx[i], ly[i], seed);
        }
        return;
    }
    if (basis == NoiseBasis::Simplex) {
        for (std::size_t i = 0; i < count; ++i) {
            noise[i] = SimplexNoise2(lx[i], ly[i], seed);
        }
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        noise[i] = BasisNoise2(basis, lx[i], ly[i], seed);
    }
}

/// `Fractal2` over one run of at most `kRun` points.
TERRAINGEN_AVX2_CLONES
void FractalRun(const float* xs, const float* ys, std::size_t count, FractalParams params,
                float* heights) {
    float lx[kRun];
    float ly[kRun];
    float noise[kRun];
    detail::FractalSums sums[kRun];
    for (std::size_t i = 0; i < count; ++i) {
        sums[i] = detail::FractalSums{};
    }
    float frequency = 1.0f / params.wavelength;
    float turnCos = 1.0f;
    float turnSin = 0.0f;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const UVec3 h = Pcg3dHash(detail::OctaveKey(params.seed, uint32_t(octave), 0u));
        const float offsetX = HashToUnitFloat(h.y);
        const float offsetY = HashToUnitFloat(h.z);
        for (std::size_t i = 0; i < count; ++i) {
            const float rx = xs[i] * turnCos - ys[i] * turnSin;
            const float ry = xs[i] * turnSin + ys[i] * turnCos;
            lx[i] = rx * frequency + offsetX;
            ly[i] = ry * frequency + offsetY;
        }
        BasisRun(params.basis, lx, ly, count, h.x, noise);
        for (std::size_t i = 0; i < count; ++i) {
            sums[i] = detail::AddFractalOctave(sums[i], params, octave, noise[i]);
        }
        frequency *= params.lacunarity;
        const float nextCos = turnCos * detail::kOctaveTurnCos - turnSin * detail::kOctaveTurnSin;
        turnSin = turnSin * detail::kOctaveTurnCos + turnCos * detail::kOctaveTurnSin;
        turnCos = nextCos;
    }
    for (std::size_t i = 0; i < count; ++i) {
        heights[i] = detail::FinishFractal(sums[i], params);
    }
}

}  // namespace

void Fractal2Rows(Span<const float> xs, Span<const float> ys, FractalParams params,
                  Span<float> heights) {
    for (std::size_t begin = 0; begin < xs.size(); begin += kRun) {
        const std::size_t count = std::min(kRun, xs.size() - begin);
        FractalRun(xs.data() + begin, ys.data() + begin, count, params, heights.data() + begin);
    }
}

void EvaluateFractalsRows(Span<const float> xs, Span<const float> ys, const Fractal* fractals,
                          std::size_t count, Span<float> heights) {
    const std::size_t points = xs.size();
    std::vector<detail::NoiseStack> stacks(points);
    std::vector<float> values(points);
    std::vector<float> dx(points);
    std::vector<float> dy(points);
    std::vector<float> wx(points);
    std::vector<float> wy(points);
    for (std::size_t f = 0; f < count; ++f) {
        const Fractal& layer = fractals[f];
        if (!layer.enabled) {
            continue;
        }
        FractalParams unit = FractalParamsFrom(layer);
        unit.amplitude = 1.0f;
        if (layer.warpAmplitude > 0.0f) {
            FractalParams warp = WarpParams(layer);
            Fractal2Rows(xs, ys, warp, dx);
            warp.seed = PcgHash(warp.seed);
            Fractal2Rows(xs, ys, warp, dy);
            for (std::size_t i = 0; i < points; ++i) {
                wx[i] = xs[i] + dx[i];
                wy[i] = ys[i] + dy[i];
            }
            Fractal2Rows(wx, wy, unit, values);
        } else {
            Fractal2Rows(xs, ys, unit, values);
        }
        for (std::size_t i = 0; i < points; ++i) {
            stacks[i] = detail::BlendFractal(stacks[i], layer, values[i]);
        }
    }
    for (std::size_t i = 0; i < points; ++i) {
        heights[i] = stacks[i].height;
    }
}

}  // namespace terraingen
