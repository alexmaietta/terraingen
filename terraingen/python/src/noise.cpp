#include "bindings.h"
#include "common.h"

#include "terraingen/noise/fractal.h"
#include "terraingen/noise/hash.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/noise/simplex.h"
#include "terraingen/noise/validate.h"
#include "terraingen/noise/value.h"
#include "terraingen/noise/worley.h"
#include "terraingen/sources/synthetic_terrain.h"

#include <nanobind/ndarray.h>
#include <nanobind/operators.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraingen::python {

namespace {

using InputArray = nb::ndarray<const float, nb::device::cpu, nb::c_contig>;
using InputArray64 = nb::ndarray<const double, nb::device::cpu, nb::c_contig>;
using OutputArray = nb::ndarray<nb::numpy, float>;

/// @brief The shape shared by every array, or an error naming them.
template <typename Array, typename... More>
std::vector<std::size_t> SameShape(const char* names, const Array& first, const More&... more) {
    std::vector<std::size_t> shape(first.ndim());
    for (std::size_t i = 0; i < first.ndim(); ++i) {
        shape[i] = first.shape(i);
    }
    const auto matches = [&](const Array& other) {
        if (other.ndim() != first.ndim()) {
            return false;
        }
        for (std::size_t i = 0; i < first.ndim(); ++i) {
            if (other.shape(i) != first.shape(i)) {
                return false;
            }
        }
        return true;
    };
    if (!(matches(more) && ...)) {
        throw std::invalid_argument(std::string(names) + " must have the same shape");
    }
    return shape;
}

/// @brief A new float32 array of `shape`, owning `data`.
OutputArray NewArray(const std::vector<std::size_t>& shape, std::size_t count, float*& data) {
    data = new float[count];
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<float*>(p); });
    return OutputArray(data, shape.size(), shape.data(), owner);
}

/// @brief `fn(x, y)` at each point of two float32 arrays of one shape, without the GIL.
template <typename Fn> OutputArray Evaluate(const InputArray& x, const InputArray& y, Fn fn) {
    const std::vector<std::size_t> shape = SameShape("x and y", x, y);
    float* out = nullptr;
    OutputArray result = NewArray(shape, x.size(), out);
    {
        nb::gil_scoped_release release;
        for (std::size_t i = 0; i < x.size(); ++i) {
            out[i] = fn(x.data()[i], y.data()[i]);
        }
    }
    return result;
}

/// @brief `fn(x, y, z)` at each point of three float64 arrays of one shape, as float32.
template <typename Fn>
OutputArray Evaluate3(const InputArray64& x, const InputArray64& y, const InputArray64& z, Fn fn) {
    const std::vector<std::size_t> shape = SameShape("x, y and z", x, y, z);
    float* out = nullptr;
    OutputArray result = NewArray(shape, x.size(), out);
    {
        nb::gil_scoped_release release;
        for (std::size_t i = 0; i < x.size(); ++i) {
            out[i] = fn(x.data()[i], y.data()[i], z.data()[i]);
        }
    }
    return result;
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

/// @brief Binds `name` for arrays and for one point.
template <float (*Noise)(float, float, std::uint32_t)>
void BindScalarNoise(nb::module_& m, const char* name, const char* doc) {
    m.def(
        name,
        [](const InputArray& x, const InputArray& y, std::uint32_t seed) {
            return Evaluate(x, y, [seed](float px, float py) { return Noise(px, py, seed); });
        },
        "x"_a, "y"_a, "seed"_a, doc);
    m.def(
        name, [](float x, float y, std::uint32_t seed) { return Noise(x, y, seed); }, "x"_a, "y"_a,
        "seed"_a);
}

float PerlinAt(float x, float y, std::uint32_t seed) {
    return PerlinNoise2(x, y, seed);
}

float SimplexAt(float x, float y, std::uint32_t seed) {
    return SimplexNoise2(x, y, seed);
}

float ValueAt(float x, float y, std::uint32_t seed) {
    return ValueNoise2(x, y, seed);
}

nb::tuple WorleyArrays(const InputArray& x, const InputArray& y, std::uint32_t seed) {
    const std::vector<std::size_t> shape = SameShape("x and y", x, y);
    float* f1 = nullptr;
    float* f2 = nullptr;
    OutputArray first = NewArray(shape, x.size(), f1);
    OutputArray second = NewArray(shape, x.size(), f2);
    {
        nb::gil_scoped_release release;
        for (std::size_t i = 0; i < x.size(); ++i) {
            const WorleyDistances d = Worley2(x.data()[i], y.data()[i], seed);
            f1[i] = d.f1;
            f2[i] = d.f2;
        }
    }
    return nb::make_tuple(first, second);
}

void BindBasisNoise(nb::module_& m) {
    BindScalarNoise<&PerlinAt>(m, "perlin_noise2",
                               "Improved Perlin noise (Perlin 2002) in [-1, 1], in lattice units.");
    BindScalarNoise<&SimplexAt>(m, "simplex_noise2",
                                "Simplex noise (Perlin 2001, Gustavson 2005) in [-1, 1].");
    BindScalarNoise<&ValueAt>(m, "value_noise2", "Value noise in [-1, 1], in lattice units.");
    m.def("worley2", &WorleyArrays, "x"_a, "y"_a, "seed"_a,
          "Worley cellular distances (F1, F2) to the nearest and second-nearest feature points "
          "(Worley 1996), in lattice units.");
    m.def(
        "worley2",
        [](float x, float y, std::uint32_t seed) {
            const WorleyDistances d = Worley2(x, y, seed);
            return nb::make_tuple(d.f1, d.f2);
        },
        "x"_a, "y"_a, "seed"_a);
}

void BindEnums(nb::module_& m) {
    nb::enum_<NoiseBasis>(m, "NoiseBasis", "Single-octave noise a fractal is built from.")
        .value("PERLIN", NoiseBasis::Perlin)
        .value("SIMPLEX", NoiseBasis::Simplex)
        .value("VALUE", NoiseBasis::Value)
        .value("WORLEY_F1", NoiseBasis::WorleyF1)
        .value("WORLEY_F2", NoiseBasis::WorleyF2)
        .value("WORLEY_F2_MINUS_F1", NoiseBasis::WorleyF2MinusF1);
    nb::enum_<FractalType>(m, "FractalType", "How octaves of the basis noise are combined.")
        .value("FBM", FractalType::Fbm)
        .value("RIDGED", FractalType::Ridged)
        .value("BILLOW", FractalType::Billow)
        .value("HYBRID_MULTIFRACTAL", FractalType::HybridMultifractal);
    nb::enum_<LayerBlend>(m, "LayerBlend",
                          "How a layer combines with the height of the layers before it.")
        .value("ADD", LayerBlend::Add)
        .value("MAX", LayerBlend::Max)
        .value("MIN", LayerBlend::Min)
        .value("MASK", LayerBlend::Mask);
}

void BindFractalParams(nb::module_& m) {
    nb::class_<FractalParams>(m, "FractalParams",
                              "Fractal of a basis noise. Octave i has wavelength "
                              "wavelength / lacunarity**i and weight gain**i.")
        .def(
            "__init__",
            [](FractalParams* p, std::uint32_t seed, float wavelength, float amplitude, int octaves,
               float lacunarity, float gain, NoiseBasis basis, FractalType type, float offset) {
                new (p) FractalParams{seed, wavelength, amplitude, octaves, lacunarity,
                                      gain, basis,      type,      offset};
            },
            "seed"_a = 0u, "wavelength"_a = 1000.0f, "amplitude"_a = 100.0f, "octaves"_a = 8,
            "lacunarity"_a = 2.0f, "gain"_a = 0.5f, "basis"_a = NoiseBasis::Perlin,
            "type"_a = FractalType::Fbm, "offset"_a = 1.0f)
        .def_rw("seed", &FractalParams::seed)
        .def_rw("wavelength", &FractalParams::wavelength, "First-octave wavelength in metres.")
        .def_rw("amplitude", &FractalParams::amplitude, "Maximum absolute height in metres.")
        .def_rw("octaves", &FractalParams::octaves)
        .def_rw("lacunarity", &FractalParams::lacunarity)
        .def_rw("gain", &FractalParams::gain)
        .def_rw("basis", &FractalParams::basis)
        .def_rw("type", &FractalParams::type)
        .def_rw("offset", &FractalParams::offset, "Ridge offset for ridged and hybrid fractals.")
        .def(nb::self == nb::self);
}

void BindFractal(nb::module_& m) {
    BindFractalParams(m);
    m.def("gain_from_hurst", &GainFromHurst, "hurst"_a, "lacunarity"_a = 2.0f,
          "Octave gain lacunarity**(-hurst) that gives Hurst exponent `hurst`.");
    m.def("basis_noise2", &BasisNoise2, "basis"_a, "x"_a, "y"_a, "seed"_a,
          "One octave of `basis` noise in [-1, 1], in lattice units.");
    m.def("validate_fractal_params", &ValidateFractalParams, "params"_a,
          "Raise ValueError if the parameters are unusable.");
    m.def(
        "fractal2",
        [](const InputArray& x, const InputArray& y, const FractalParams& params) {
            ValidateFractalParams(params);
            return Evaluate(x, y,
                            [&params](float px, float py) { return Fractal2(px, py, params); });
        },
        "x"_a, "y"_a, "params"_a, "Fractal height in metres at each world position (x, y).");
    m.def(
        "fractal2",
        [](float x, float y, const FractalParams& params) {
            ValidateFractalParams(params);
            return Fractal2(x, y, params);
        },
        "x"_a, "y"_a, "params"_a);
}

void BindNoiseLayer(nb::module_& m) {
    nb::class_<NoiseLayer>(m, "NoiseLayer",
                           "One layer of a synthetic terrain: a fractal, an optional domain warp "
                           "and a blend mode.")
        .def(
            "__init__",
            [](NoiseLayer* layer, std::optional<FractalParams> fractal, bool enabled,
               LayerBlend blend, float warpAmplitude, float warpWavelength) {
                new (layer) NoiseLayer{enabled, fractal.value_or(FractalParams{}), blend,
                                       warpAmplitude, warpWavelength};
            },
            "fractal"_a = nb::none(), "enabled"_a = true, "blend"_a = LayerBlend::Add,
            "warp_amplitude"_a = 0.0f, "warp_wavelength"_a = 500.0f)
        .def_rw("enabled", &NoiseLayer::enabled)
        .def_rw("fractal", &NoiseLayer::fractal)
        .def_rw("blend", &NoiseLayer::blend)
        .def_rw("warp_amplitude", &NoiseLayer::warpAmplitude, "Domain warp in metres; 0 is off.")
        .def_rw("warp_wavelength", &NoiseLayer::warpWavelength, "Warp field wavelength in metres.")
        .def(nb::self == nb::self);
    m.attr("MAX_NOISE_LAYERS") = kMaxNoiseLayers;
    m.def("validate_noise_layer", &ValidateNoiseLayer, "layer"_a,
          "Raise ValueError if the layer is unusable.");
}

void BindLayerEvaluation(nb::module_& m) {
    m.def(
        "evaluate_noise_layers",
        [](const InputArray& x, const InputArray& y, const SyntheticTerrainParams& params) {
            ValidateSyntheticTerrain(params);
            return Evaluate(x, y, [&params](float px, float py) {
                return EvaluateNoiseLayers(px, py, params.layers.data(), params.layers.size());
            });
        },
        "x"_a, "y"_a, "params"_a, "Height in metres of the layer stack at each world position.");
    m.def(
        "evaluate_noise_layer",
        [](float x, float y, const NoiseLayer& layer) {
            ValidateNoiseLayer(layer);
            return EvaluateNoiseLayer(x, y, layer);
        },
        "x"_a, "y"_a, "layer"_a,
        "The normalized value in [-1, 1] of one layer at world position (x, y), before amplitude "
        "and blending.");
    m.def(
        "domain_warp2",
        [](float x, float y, const NoiseLayer& layer) {
            const WarpOffset2 offset = DomainWarp2(x, y, layer);
            return nb::make_tuple(offset.dx, offset.dy);
        },
        "x"_a, "y"_a, "layer"_a,
        "The domain warp offset (dx, dy) in metres a layer applies at world position (x, y).");
}

void BindBasisNoise3(nb::module_& m) {
    m.def(
        "perlin_noise3",
        [](const InputArray64& x, const InputArray64& y, const InputArray64& z,
           std::uint32_t seed) {
            return Evaluate3(x, y, z, [seed](double px, double py, double pz) {
                return PerlinNoise3Precise(px, py, pz, seed);
            });
        },
        "x"_a, "y"_a, "z"_a, "seed"_a,
        "3D improved Perlin noise in [-1, 1] at each point, in lattice units; the cell is found in "
        "double precision, so it stays exact far from the origin.");
    m.def("perlin_noise3", &PerlinNoise3Precise, "x"_a, "y"_a, "z"_a, "seed"_a);
    m.def(
        "value_noise3",
        [](double x, double y, double z, std::uint32_t seed) {
            return ValueNoise3(SplitLatticePrecise3(x, y, z), seed);
        },
        "x"_a, "y"_a, "z"_a, "seed"_a, "3D value noise in [-1, 1], in lattice units.");
    m.def("has_basis_noise3", &HasBasisNoise3, "basis"_a,
          "Whether `basis` has a 3D form (Perlin and value noise).");
    m.def(
        "basis_noise3",
        [](NoiseBasis basis, std::int32_t ix, std::int32_t iy, std::int32_t iz, float fx, float fy,
           float fz, std::uint32_t seed) {
            return BasisNoise3(basis, LatticePoint3{ix, iy, iz, fx, fy, fz}, seed);
        },
        "basis"_a, "ix"_a, "iy"_a, "iz"_a, "fx"_a, "fy"_a, "fz"_a, "seed"_a,
        "One octave of 3D `basis` noise at lattice cell (ix, iy, iz) plus fraction (fx, fy, fz).");
}

void BindFractal3(nb::module_& m) {
    m.def(
        "fractal3",
        [](const InputArray64& x, const InputArray64& y, const InputArray64& z,
           const FractalParams& params) {
            ValidateFractalParams(params);
            return Evaluate3(x, y, z, [&params](double px, double py, double pz) {
                return Fractal3(px, py, pz, params);
            });
        },
        "x"_a, "y"_a, "z"_a, "params"_a,
        "Fractal height in metres at each 3D world position (m), as noise on a sphere uses it.");
    m.def(
        "fractal3",
        [](double x, double y, double z, const FractalParams& params) {
            ValidateFractalParams(params);
            return Fractal3(x, y, z, params);
        },
        "x"_a, "y"_a, "z"_a, "params"_a);
    m.def(
        "evaluate_noise_layers3",
        [](const InputArray64& x, const InputArray64& y, const InputArray64& z,
           const SyntheticTerrainParams& params) {
            ValidateSyntheticTerrain(params);
            return Evaluate3(x, y, z, [&params](double px, double py, double pz) {
                return EvaluateNoiseLayers3(px, py, pz, params.layers.data(), params.layers.size());
            });
        },
        "x"_a, "y"_a, "z"_a, "params"_a,
        "Height in metres of the layer stack at each 3D world position (m).");
}

}  // namespace

void BindNoise(nb::module_& m) {
    BindHashes(m);
    BindBasisNoise(m);
    BindEnums(m);
    BindFractal(m);
    BindNoiseLayer(m);
    BindLayerEvaluation(m);
    BindBasisNoise3(m);
    BindFractal3(m);
}

}  // namespace terraingen::python
