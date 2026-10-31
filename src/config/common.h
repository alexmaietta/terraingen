// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief What every area of a configuration shares: paths and errors, boxed values, surface
/// fields, density maps and noise stacks.
///
/// Each configuration struct has a `Fields(IO& io, Struct& value)` beside it, in namespace
/// `terraingen` like the library's (`terraingen/util/option_fields.h`), so the same list is read,
/// written, range checked and described by passing a different `IO`. Each alternative of a variant
/// also has a `SpecName`.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/json.h"
#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/geometry_fields.h"
#include "terraingen/layers/density_map.h"
#include "terraingen/layers/field.h"
#include "terraingen/layers/layer_fields.h"
#include "terraingen/noise/noise_fields.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/raster_fields.h"
#include "terraingen/raster/resample.h"
#include "terraingen/util/option_fields.h"

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

/// A value held by pointer, so that a spec can hold the variant it belongs to; it is copied by
/// value.
template <typename T> class Boxed {
  public:
    /// A default-constructed value.
    Boxed() : value_(std::make_unique<T>()) {}
    /// A value made from anything `T` can be made from, such as one alternative of a variant.
    /// `std::conjunction` stops at a `Boxed` argument, so copying never asks whether an incomplete
    /// `T` is constructible.
    template <
        typename U,
        std::enable_if_t<
            std::conjunction_v<
                std::negation<std::is_same<std::remove_cv_t<std::remove_reference_t<U>>, Boxed>>,
                std::is_constructible<T, U&&>>,
            int> = 0>
    Boxed(U&& value)  // NOLINT(google-explicit-constructor): converts implicitly by design
        : value_(std::make_unique<T>(std::forward<U>(value))) {}
    Boxed(const Boxed& other) : value_(std::make_unique<T>(*other.value_)) {}
    Boxed(Boxed&& other) noexcept = default;
    Boxed& operator=(const Boxed& other) {
        value_ = std::make_unique<T>(*other.value_);
        return *this;
    }
    Boxed& operator=(Boxed&& other) noexcept = default;
    ~Boxed() = default;

    T& operator*() {
        return *value_;
    }
    const T& operator*() const {
        return *value_;
    }
    T* operator->() {
        return value_.get();
    }
    const T* operator->() const {
        return value_.get();
    }

  private:
    std::unique_ptr<T> value_;
};

/// Checks the ranges of a boxed value as of the value itself.
template <typename T>
void CheckValueRanges(Boxed<T>& value, const std::string& path, std::string_view unit,
                      const FieldRange& range) {
    CheckValueRanges(*value, path, unit, range);
}

/// A scalar field over the surface (`SurfaceField`): a constant `value`, or a GeoTIFF band read
/// in the surface's coordinates (`RasterField`) when `path` is set.
struct FieldSpec {
    std::string path{};    ///< GeoTIFF, relative to the configuration's folder; empty for `value`.
    double value = 0.0;    ///< The field everywhere, without a `path`.
    std::size_t band = 0;  ///< The GeoTIFF band, from 0.
    RasterFieldOptions read{};
};

template <typename IO> void Fields(IO& io, FieldSpec& field) {
    io("path", field.path,
       "GeoTIFF read in the surface's coordinates, relative to the configuration's folder: "
       "meters on a plane, longitude and latitude in degrees on a sphere. Empty uses value.");
    io("value", field.value, "The field everywhere when no path is given.");
    io("band", field.band, "Band of the GeoTIFF read, from 0.");
    Fields(io, field.read);
}

/// A map of a population, `DensityMap` or `SizeSlopeMap`: `map` with its field read from `field`.
template <typename Map> struct MapSpec {
    FieldSpec field{};
    Map map{};  ///< Its `field` is left null; `field` gives it.
};

/// A density map of a crater or boulder population.
using DensityMapSpec = MapSpec<DensityMap>;

/// A size slope map of a boulder population.
using SizeSlopeMapSpec = MapSpec<SizeSlopeMap>;

template <typename IO, typename Map> void Fields(IO& io, MapSpec<Map>& spec) {
    io("field", spec.field, "The map.");
    Fields(io, spec.map);
}

/// Amplitude over wavelength of a terrain noise layer whose amplitude is unset, near the lunar
/// highland median slope (Rosenburg et al. 2011, https://doi.org/10.1029/2010JE003716).
inline constexpr float kNoiseAmplitudeRatio = 0.1f;

/// One fractal of a noise stack, its octave gain optionally derived from a Hurst exponent.
struct FractalSpec {
    Fractal fractal{};
    /// When set, the octave gain comes from it (`GainFromHurst`) instead of `fractal.gain`.
    std::optional<float> hurst;
    /// When set, the amplitude in place of `fractal.amplitude`; unset gives terrain noise
    /// `kNoiseAmplitudeRatio` times its wavelength (`EffectiveRelief`).
    std::optional<float> amplitude;

    /// `fractal` with its gain replaced from `hurst` and its amplitude from `amplitude` when set.
    Fractal Effective() const;
    /// `Effective` with an unset amplitude `kNoiseAmplitudeRatio` times the wavelength, as terrain
    /// noise layers take it.
    Fractal EffectiveRelief() const;
};

/// One fractal of an albedo noise stack, its amplitude in what the stack varies, relative
/// brightness, saturation or degrees of hue, rather than in meters, and required.
struct AlbedoFractalSpec : FractalSpec {};

/// The fields of a noise layer, its amplitude written as `amplitudeKey` in `amplitudeUnit`.
template <typename IO>
void FractalSpecFields(IO& io, FractalSpec& spec, std::string_view amplitudeKey,
                       std::string_view amplitudeUnit, std::string_view amplitudeHelp,
                       const FieldRange& amplitudeRange = {}) {
    FractalBaseFields(io, spec.fractal);
    io(amplitudeKey, spec.amplitude, amplitudeHelp, amplitudeUnit, amplitudeRange);
    FractalOctaveFields(io, spec.fractal);
    io("hurst", spec.hurst,
       "Hurst exponent that sets the octave gain to lacunarity^-hurst: near 1 is smooth, lower is "
       "rougher at short baselines. Unset uses the gain.");
    FractalWarpFields(io, spec.fractal);
}

template <typename IO> void Fields(IO& io, FractalSpec& spec) {
    FractalSpecFields(io, spec, "amplitude_m", "m",
                      "Largest height of the layer above or below 0, in meters; unset is 0.1 times "
                      "the wavelength, near the lunar highland median slope at any wavelength.");
}

template <typename IO> void Fields(IO& io, AlbedoFractalSpec& spec) {
    FractalSpecFields(io, spec, "amplitude", "",
                      "Largest value of the layer above or below 0, in what its stack varies: "
                      "relative brightness, saturation or degrees of hue.",
                      Required());
}

/// The description of a terrain noise stack.
inline constexpr std::string_view kNoiseStackHelp =
    "Noise layers, combined in order: the first enabled layer sets the height and each later one "
    "is combined with it by its blend mode.";

/// The length of a noise stack: `fewest` to `kMaxFractals` layers.
inline FieldRange NoiseStackLength(std::size_t fewest) {
    return Length(fewest, kMaxFractals);
}

/// The fractals of a stack of noise layers: each layer's Hurst exponent applied and an unset
/// amplitude `kNoiseAmplitudeRatio` times its wavelength (`FractalSpec::EffectiveRelief`).
std::vector<Fractal> StackFractals(const std::vector<FractalSpec>& stack);

/// The fractal a synthetic source starts from by default: fBm 400 m in wavelength and 12 m in
/// amplitude, seed 2.
inline FractalSpec DefaultBaseNoise() {
    FractalSpec base;
    base.fractal.seed = 2;
    base.fractal.wavelength = 400.0f;
    base.fractal.amplitude = 12.0f;
    base.amplitude = 12.0f;
    return base;
}

/// Reading, checking and building configurations; the specs themselves sit in `terraingen`
/// beside the library's option structs.
namespace config {

/// Whether a variant, such as a layer or an output, holds one of the alternatives `Kinds`.
template <typename... Kinds, typename... Ts> bool HoldsAny(const std::variant<Ts...>& value) {
    return (std::holds_alternative<Kinds>(value) || ...);
}

/// "lowest to highest", as messages give the counts a value allows.
inline std::string CountText(std::size_t lowest, std::size_t highest) {
    return std::to_string(lowest) + " to " + std::to_string(highest);
}

/// Checks that a configuration string, such as a file path, is not empty.
/// @throws ConfigError "must not be empty" at `path` otherwise.
inline void CheckNonEmpty(const std::string& value, const std::string& path) {
    if (value.empty()) {
        throw ConfigError(path, "must not be empty");
    }
}

/// Runs a library check or build step `check()` and returns its result, reporting its errors at
/// the configuration path `path`, a field out of its range at its own path below it.
/// @throws ConfigError at `path` with the message of a std::invalid_argument or std::domain_error
/// that `check` throws.
template <typename Check> auto At(const std::string& path, Check check) {
    try {
        return check();
    } catch (const FieldRangeError& error) {
        throw ConfigError(FieldPath(path, error.Path()), error.Rule());
    } catch (const std::invalid_argument& error) {
        throw ConfigError(path, error.what());
    } catch (const std::domain_error& error) {
        throw ConfigError(path, error.what());
    }
}

/// Checks every number of a configuration value at `path` against the range its field declares
/// (`FieldRange`): every number finite, every list of a length its field allows and every required
/// optional set.
/// @throws ConfigError naming the first value that does not fit by its JSON path.
template <typename T> void CheckConfigRanges(const T& value, const std::string& path = {}) {
    T copy = value;
    try {
        CheckValueRanges(copy, path, {}, FieldRange{});
    } catch (const FieldRangeError& error) {
        throw ConfigError(error.Path(), error.Rule());
    }
}

/// Runs a step `read()` that reads a file, such as a GeoTIFF or a mesh, and returns its result,
/// reporting any failure at the configuration path `path` of the file.
/// @throws ConfigError at `path` with the message of any exception `read` throws.
template <typename Read> auto AtFile(const std::string& path, Read read) {
    try {
        return read();
    } catch (const ConfigError&) {
        throw;
    } catch (const std::exception& error) {
        throw ConfigError(path, error.what());
    }
}

/// `path` resolved against `directory`; an empty `directory` leaves it relative.
std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory);

/// The description of an output file's path: `file`, such as "GeoTIFF file to write", that it may
/// be left unset, and where relative paths resolve.
inline std::string OutputPathHelp(std::string_view file) {
    return std::string(file) +
           "; unset to only return the result, writing nothing; relative paths resolve against the "
           "output folder (the CLI's --output-dir), else the configuration's folder.";
}

/// The description of an input file's path: `file`, such as "GeoTIFF to read the heights from",
/// and where relative paths resolve.
inline std::string InputPathHelp(std::string_view file) {
    return std::string(file) + "; relative paths resolve against the configuration's folder.";
}

/// @throws ConfigError at `path` for a grid side outside the sizes a configuration may request.
void CheckGridSize(std::size_t width, std::size_t height, const std::string& path);

/// @throws ConfigError at `path` for a band the GeoTIFF `info` describes does not have.
void CheckBand(std::size_t band, const GeoTiffInfo& info, const std::string& path);

/// @throws ConfigError at `path` for an expected count of `items` above the generation limit,
/// suggesting `remedy`.
void CheckExpectedCount(double expected, std::string_view items, std::string_view remedy,
                        const std::string& path);

/// @throws ConfigError at the first invalid layer of a noise stack.
template <typename Spec>
void CheckNoiseStack(const std::vector<Spec>& layers, const std::string& path) {
    for (std::size_t i = 0; i < layers.size(); ++i) {
        At(ItemPath(path, i), [&] { ValidateFractal(layers[i].Effective()); });
    }
}

/// The field a spec describes: a constant, or its GeoTIFF read.
/// @param path The spec's JSON path, which errors are reported under.
/// @param directory Folder the GeoTIFF resolves against.
/// @throws ConfigError at `path` for an unreadable GeoTIFF or invalid read options.
std::shared_ptr<const SurfaceField> BuildField(const FieldSpec& spec, const std::string& path,
                                               const std::filesystem::path& directory = {});

/// The map a spec describes, its field built (`BuildField`) and the map checked.
/// @throws ConfigError at `path`.field for an unreadable GeoTIFF, or at `path` for an invalid map.
template <typename Map>
Map BuildMap(const MapSpec<Map>& spec, const std::string& path,
             const std::filesystem::path& directory = {}) {
    Map map = spec.map;
    map.field = BuildField(spec.field, FieldPath(path, "field"), directory);
    At(path, [&] {
        if constexpr (std::is_same_v<Map, DensityMap>) {
            ValidateDensityMap(map);
        } else {
            ValidateSizeSlopeMap(map);
        }
    });
    return map;
}

}  // namespace config

}  // namespace terraingen
