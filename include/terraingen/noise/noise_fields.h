// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of fractals and the names of the noise enums.
#pragma once

#include "terraingen/noise/fractals.h"
#include "terraingen/util/option_fields.h"

#include <array>

namespace terraingen {

template <> struct EnumNames<NoiseBasis> {
    static constexpr std::array<EnumEntry<NoiseBasis>, 7> kValues{{
        {"perlin", NoiseBasis::Perlin,
         "Improved Perlin gradient noise: smooth, general-purpose relief."},
        {"simplex", NoiseBasis::Simplex,
         "Simplex gradient noise: smooth relief, an alternative to Perlin."},
        {"value", NoiseBasis::Value,
         "Value noise: random values interpolated over a lattice; blockier than gradient noise."},
        {"worley_f1", NoiseBasis::WorleyF1,
         "Cellular noise from the distance to the nearest feature point: cones and basins."},
        {"worley_f2", NoiseBasis::WorleyF2,
         "Cellular noise from the distance to the second-nearest feature point."},
        {"worley_f2_minus_f1", NoiseBasis::WorleyF2MinusF1,
         "Cellular noise F2 - F1: ridges along cell borders, like cracked ground."},
        {"voronoi", NoiseBasis::Voronoi,
         "Voronoi cells, each at one random height: flat polygonal plateaus and terraces."},
    }};
};

template <> struct EnumNames<FractalType> {
    static constexpr std::array<EnumEntry<FractalType>, 4> kValues{{
        {"fbm", FractalType::Fbm,
         "Fractional Brownian motion: a weighted sum of octaves, the general-purpose choice."},
        {"ridged", FractalType::Ridged,
         "Ridged multifractal: sharp crests from inverted absolute noise, like eroded ridges."},
        {"billow", FractalType::Billow, "Sum of absolute-value octaves: rounded, puffy forms."},
        {"hybrid_multifractal", FractalType::HybridMultifractal,
         "Hybrid multifractal: smooth lowlands and rough highlands."},
    }};
};

template <> struct EnumNames<FractalBlend> {
    static constexpr std::array<EnumEntry<FractalBlend>, 4> kValues{{
        {"add", FractalBlend::Add, "Adds the layer to the height so far."},
        {"max", FractalBlend::Max, "Keeps the higher of the height so far and the layer."},
        {"min", FractalBlend::Min, "Keeps the lower of the height so far and the layer."},
        {"mask", FractalBlend::Mask,
         "Multiplies the height so far by (layer / amplitude + 1) / 2, so the layer modulates "
         "roughness by region."},
    }};
};

/// The fields of a fractal before its amplitude: whether it is on, its noise, blend, seed and
/// wavelength.
template <typename IO> void FractalBaseFields(IO& io, Fractal& fractal) {
    io("enabled", fractal.enabled,
       "Includes this layer in the stack; disabled layers are skipped.");
    io("basis", fractal.basis, "Single-octave noise the fractal is built from.");
    io("fractal", fractal.type, "How the octaves of the basis noise are combined.");
    io("blend", fractal.blend,
       "How this layer combines with the height of the layers before it; the first enabled layer "
       "sets the height instead.");
    io("seed", fractal.seed,
       "Seed of the noise; changing it gives a different pattern of the same character.");
    io("wavelength_m", fractal.wavelength,
       "Wavelength of the first, broadest octave, in meters; sets the size of the largest "
       "features.",
       "m", Above(0.0));
}

/// The fields of a fractal's octaves after its amplitude: their count, lacunarity and gain.
template <typename IO> void FractalOctaveFields(IO& io, Fractal& fractal) {
    io("octaves", fractal.octaves,
       "Number of octaves; each one adds detail at a shorter wavelength.", "",
       Between(1.0, kMaxFractalOctaves));
    io("lacunarity", fractal.lacunarity, "Frequency ratio between successive octaves.", "",
       Above(1.0));
    io("gain", fractal.gain,
       "Weight ratio between successive octaves; higher values give rougher terrain. Unused when "
       "the Hurst exponent is set.",
       "", Above(0.0));
}

/// The fields of a fractal's ridge offset and domain warp.
template <typename IO> void FractalWarpFields(IO& io, Fractal& fractal) {
    io("offset", fractal.offset, "Ridge offset of the ridged and hybrid multifractal types.", "",
       Above(0.0));
    io("warp_amplitude_m", fractal.warpAmplitude,
       "Domain warp displacement in meters, which bends the noise into swirled shapes; 0 "
       "disables warping.",
       "m", AtLeast(0.0));
    io("warp_wavelength_m", fractal.warpWavelength, "Wavelength of the warp field, in meters.", "m",
       Above(0.0));
}

template <typename IO> void Fields(IO& io, Fractal& fractal) {
    FractalBaseFields(io, fractal);
    io("amplitude_m", fractal.amplitude, "Largest height of the layer above or below 0, in meters.",
       "m");
    FractalOctaveFields(io, fractal);
    FractalWarpFields(io, fractal);
}

}  // namespace terraingen
