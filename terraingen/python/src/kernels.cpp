#include "bindings.h"

#include "terraingen/kernels/fractal.h"
#include "terraingen/kernels/hash.h"
#include "terraingen/kernels/layers.h"
#include "terraingen/kernels/perlin.h"
#include "terraingen/kernels/simplex.h"
#include "terraingen/kernels/value.h"
#include "terraingen/kernels/worley.h"
#include "terraingen/surfaces/synthetic_terrain.h"

#include <nanobind/ndarray.h>
#include <nanobind/operators.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

using InputArray = nb::ndarray<const float, nb::device::cpu, nb::c_contig>;
using OutputArray = nb::ndarray<nb::numpy, float>;

std::vector<std::size_t> SameShape(const InputArray& x, const InputArray& y) {
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
    return shape;
}

OutputArray NewArray(const std::vector<std::size_t>& shape, std::size_t count, float*& data) {
    data = new float[count];
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<float*>(p); });
    return OutputArray(data, shape.size(), shape.data(), owner);
}

template <typename Fn> OutputArray Evaluate(const InputArray& x, const InputArray& y, Fn fn) {
    const std::vector<std::size_t> shape = SameShape(x, y);
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

void BindBasisNoise(nb::module_& m) {
    BindScalarNoise<&PerlinAt>(m, "perlin_noise2",
                               "Improved Perlin noise (Perlin 2002) in [-1, 1], in lattice units.");
    BindScalarNoise<&SimplexAt>(m, "simplex_noise2",
                                "Simplex noise (Perlin 2001, Gustavson 2005) in [-1, 1].");
    BindScalarNoise<&ValueAt>(m, "value_noise2", "Value noise in [-1, 1], in lattice units.");

    m.def(
        "worley2",
        [](const InputArray& x, const InputArray& y, std::uint32_t seed) {
            const std::vector<std::size_t> shape = SameShape(x, y);
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
        },
        "x"_a, "y"_a, "seed"_a,
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

void BindFractal(nb::module_& m) {
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

void BindLayers(nb::module_& m) {
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

    nb::class_<SyntheticTerrainParams>(m, "SyntheticTerrainParams",
                                       "A stack of 1 to 8 noise layers. `layers` is copied on "
                                       "access; assign a new list to change it.")
        .def(
            "__init__",
            [](SyntheticTerrainParams* params, std::optional<std::vector<NoiseLayer>> layers) {
                new (params) SyntheticTerrainParams{};
                if (layers) {
                    params->layers = *layers;
                }
            },
            "layers"_a = nb::none())
        .def_rw("layers", &SyntheticTerrainParams::layers)
        .def(nb::self == nb::self);

    m.attr("MAX_NOISE_LAYERS") = kMaxNoiseLayers;
    m.def("validate_noise_layer", &ValidateNoiseLayer, "layer"_a,
          "Raise ValueError if the layer is unusable.");
    m.def("validate_synthetic_terrain", &ValidateSyntheticTerrain, "params"_a,
          "Raise ValueError if the terrain parameters are unusable.");
    m.def(
        "evaluate_noise_layers",
        [](const InputArray& x, const InputArray& y, const SyntheticTerrainParams& params) {
            ValidateSyntheticTerrain(params);
            return Evaluate(x, y, [&params](float px, float py) {
                return EvaluateNoiseLayers(px, py, params.layers.data(), params.layers.size());
            });
        },
        "x"_a, "y"_a, "params"_a, "Height in metres of the layer stack at each world position.");
    m.def("fill_synthetic_terrain", &FillSyntheticTerrain, "raster"_a, "params"_a,
          nb::call_guard<nb::gil_scoped_release>(),
          "Set every cell of `raster` to the synthetic terrain height at its centre.");
    m.def("generate_synthetic_terrain", &GenerateSyntheticTerrain, "width"_a, "height"_a,
          "pixel_size"_a, "params"_a, nb::call_guard<nb::gil_scoped_release>(),
          "Generate a north-up synthetic terrain centred on the world origin, with square pixels "
          "of `pixel_size` metres.");
}

}  // namespace

void BindKernels(nb::module_& m) {
    BindHashes(m);
    BindBasisNoise(m);
    BindEnums(m);
    BindFractal(m);
    BindLayers(m);
}

}  // namespace terraingen::python
