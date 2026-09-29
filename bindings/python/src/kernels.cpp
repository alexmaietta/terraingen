#include "bindings.h"

#include "terraingen/kernels/fbm.h"
#include "terraingen/kernels/hash.h"
#include "terraingen/kernels/perlin.h"
#include "terraingen/models/fbm_terrain.h"

#include <nanobind/ndarray.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

using InputArray = nb::ndarray<const float, nb::device::cpu, nb::c_contig>;
using OutputArray = nb::ndarray<nb::numpy, float>;

template <typename Fn> OutputArray Evaluate(const InputArray& x, const InputArray& y, Fn fn) {
    if (x.ndim() != y.ndim()) {
        throw std::invalid_argument("x and y must have the same shape");
    }
    std::vector<std::size_t> shape(x.ndim());
    for (std::size_t i = 0; i < x.ndim(); ++i) {
        if (x.shape(i) != y.shape(i)) {
            throw std::invalid_argument("x and y must have the same shape");
        }
        shape[i] = x.shape(i);
    }
    const std::size_t count = x.size();
    auto values = std::make_unique<float[]>(count);
    {
        nb::gil_scoped_release release;
        for (std::size_t i = 0; i < count; ++i) {
            values[i] = fn(x.data()[i], y.data()[i]);
        }
    }
    float* data = values.release();
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<float*>(p); });
    return OutputArray(data, shape.size(), shape.data(), owner);
}

void BindHashes(nb::module_& m) {
    m.def("pcg_hash", &PcgHash, "v"_a, "PCG hash of one 32-bit word (Jarzynski and Olano 2020).");
    m.def(
        "pcg3d_hash",
        [](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
            const UVec3 h = Pcg3dHash(UVec3{x, y, z});
            return nb::make_tuple(h.x, h.y, h.z);
        },
        "x"_a, "y"_a, "z"_a, "pcg3d hash of three 32-bit words (Jarzynski and Olano 2020).");
    m.def("hash_to_unit_float", &HashToUnitFloat, "hash"_a,
          "Map a hash to a float in [0, 1) using its top 24 bits.");
}

void BindPerlin(nb::module_& m) {
    m.def(
        "perlin_noise2",
        [](const InputArray& x, const InputArray& y, std::uint32_t seed) {
            return Evaluate(x, y,
                            [seed](float px, float py) { return PerlinNoise2(px, py, seed); });
        },
        "x"_a, "y"_a, "seed"_a,
        "Improved Perlin noise (Perlin 2002) in [-1, 1] at each (x, y), in lattice units.");
    m.def(
        "perlin_noise2",
        [](float x, float y, std::uint32_t seed) { return PerlinNoise2(x, y, seed); }, "x"_a, "y"_a,
        "seed"_a);
}

void BindFbm(nb::module_& m) {
    nb::class_<FbmParams>(m, "FbmParams",
                          "Fractional Brownian motion built from Perlin noise octaves. Octave i "
                          "has wavelength wavelength / lacunarity**i and weight gain**i.")
        .def(
            "__init__",
            [](FbmParams* p, std::uint32_t seed, float wavelength, float amplitude, int octaves,
               float lacunarity, float gain) {
                new (p) FbmParams{seed, wavelength, amplitude, octaves, lacunarity, gain};
            },
            "seed"_a = 0u, "wavelength"_a = 1000.0f, "amplitude"_a = 100.0f, "octaves"_a = 8,
            "lacunarity"_a = 2.0f, "gain"_a = 0.5f)
        .def_rw("seed", &FbmParams::seed)
        .def_rw("wavelength", &FbmParams::wavelength, "First-octave wavelength in metres.")
        .def_rw("amplitude", &FbmParams::amplitude, "Maximum absolute height in metres.")
        .def_rw("octaves", &FbmParams::octaves)
        .def_rw("lacunarity", &FbmParams::lacunarity)
        .def_rw("gain", &FbmParams::gain);

    m.def("gain_from_hurst", &GainFromHurst, "hurst"_a, "lacunarity"_a = 2.0f,
          "Octave gain lacunarity**(-hurst) that gives Hurst exponent `hurst`.");
    m.def("validate_fbm_params", &ValidateFbmParams, "params"_a,
          "Raise ValueError if the parameters are unusable.");
    m.def(
        "fbm2",
        [](const InputArray& x, const InputArray& y, const FbmParams& params) {
            ValidateFbmParams(params);
            return Evaluate(x, y, [&params](float px, float py) { return Fbm2(px, py, params); });
        },
        "x"_a, "y"_a, "params"_a, "fBm height in metres at each world position (x, y).");
    m.def(
        "fbm2",
        [](float x, float y, const FbmParams& params) {
            ValidateFbmParams(params);
            return Fbm2(x, y, params);
        },
        "x"_a, "y"_a, "params"_a);
    m.def("fill_fbm", &FillFbm, "raster"_a, "params"_a, nb::call_guard<nb::gil_scoped_release>(),
          "Set every cell of `raster` to the fBm height at its centre.");
}

}  // namespace

void BindKernels(nb::module_& m) {
    BindHashes(m);
    BindPerlin(m);
    BindFbm(m);
}

}  // namespace terraingen::python
