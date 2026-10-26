// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief What every configuration field list shares.
///
/// Each configuration struct has a `Fields(IO& io, Struct& value)` that calls
/// `io(key, member, description, unit, range)` for each JSON key in order; the same list is read,
/// written, range checked and described (`ConfigSchema`) by passing a different `IO` (`Reader`,
/// `Writer`, `RangeChecker` or a schema writer). Each alternative of a variant also has a
/// `SpecName` next to its `Fields`.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace terraingen::config {

/// The JSON "type" of the variant alternative `Spec` (`kName`) and what it is (`kDescription`).
template <typename Spec> struct SpecName;

/// What a field accepts on its own: the values a number, or each number of a list, may take, how
/// many items a list holds, and whether the key must be given. `ConfigSchema` and the Python
/// docstrings describe it after the field's description, `CheckConfigRanges` enforces the values,
/// lengths and required optionals, and reading JSON reports a required key that is missing. Rules
/// that tie fields together are checked by validation. Unset bounds are open.
struct FieldRange {
    std::optional<double> minimum;           ///< Smallest value, in the field's unit.
    std::optional<double> maximum;           ///< Largest value, in the field's unit.
    bool exclusiveMinimum = false;           ///< `minimum` itself is out.
    bool exclusiveMaximum = false;           ///< `maximum` itself is out.
    std::optional<std::size_t> minLength{};  ///< Fewest items of a list.
    std::optional<std::size_t> maxLength{};  ///< Most items of a list.
    /// A list holds `minLength` or `maxLength` items, none between.
    bool lengthAtBounds = false;
    /// The key must be given; an optional field must also be set.
    bool required = false;

    /// This range, with the key required.
    FieldRange Required() const {
        FieldRange range = *this;
        range.required = true;
        return range;
    }
    /// This range, for a list of exactly `count` items.
    FieldRange Length(std::size_t count) const {
        return Length(count, count);
    }
    /// This range, for a list of `fewest` to `most` items.
    FieldRange Length(std::size_t fewest, std::size_t most) const {
        FieldRange range = *this;
        range.minLength = fewest;
        range.maxLength = most;
        return range;
    }
    /// This range, for a list of at least `fewest` items.
    FieldRange MinLength(std::size_t fewest) const {
        FieldRange range = *this;
        range.minLength = fewest;
        return range;
    }
    /// This range, for a list of either `fewer` or `more` items.
    FieldRange LengthEither(std::size_t fewer, std::size_t more) const {
        FieldRange range = Length(fewer, more);
        range.lengthAtBounds = true;
        return range;
    }
    /// This range without its length or requirement, as it applies to each item of a list.
    FieldRange Items() const {
        return {minimum, maximum, exclusiveMinimum, exclusiveMaximum};
    }
    /// Whether it bounds the values.
    bool Bounded() const {
        return minimum || maximum;
    }
    /// Whether it bounds a list's length.
    bool Counted() const {
        return minLength || maxLength;
    }
};

/// Any value, its key required.
inline FieldRange Required() {
    return FieldRange{}.Required();
}

/// A list of exactly `count` items of any value.
inline FieldRange Length(std::size_t count) {
    return FieldRange{}.Length(count);
}

/// A list of `fewest` to `most` items of any value.
inline FieldRange Length(std::size_t fewest, std::size_t most) {
    return FieldRange{}.Length(fewest, most);
}

/// A list of at least `fewest` items of any value.
inline FieldRange MinLength(std::size_t fewest) {
    return FieldRange{}.MinLength(fewest);
}

/// A list of either `fewer` or `more` items of any value; `LengthEither(0, 2)` for an empty
/// (unset) list or a pair.
inline FieldRange LengthEither(std::size_t fewer, std::size_t more) {
    return FieldRange{}.LengthEither(fewer, more);
}

/// Values of at least `minimum`.
inline FieldRange AtLeast(double minimum) {
    return {minimum, std::nullopt};
}

/// Values above `minimum`; `Above(0)` for positive values.
inline FieldRange Above(double minimum) {
    return {minimum, std::nullopt, true, false};
}

/// Values from `minimum` to `maximum`, both included.
inline FieldRange Between(double minimum, double maximum) {
    return {minimum, maximum};
}

/// Values from `minimum`, included, to below `maximum`.
inline FieldRange FromBelow(double minimum, double maximum) {
    return {minimum, maximum, false, true};
}

/// Values above `minimum` up to `maximum`, included.
inline FieldRange AboveUpTo(double minimum, double maximum) {
    return {minimum, maximum, true, false};
}

/// Values above `minimum` and below `maximum`.
inline FieldRange Inside(double minimum, double maximum) {
    return {minimum, maximum, true, true};
}

/// Shares from 0 to 1.
inline FieldRange UnitInterval() {
    return Between(0.0, 1.0);
}

/// "lowest to highest", as messages give the counts a value allows.
inline std::string CountText(std::size_t lowest, std::size_t highest) {
    return std::to_string(lowest) + " to " + std::to_string(highest);
}

/// What a range allows, as an error message says it: "positive", "from 1 to 4096 deg".
/// @param unit The unit symbol given after the bounds, or empty.
std::string DescribeRange(const FieldRange& range, std::string_view unit = {});

/// What a range requires of a value, as a message says it: "must be positive", "must not be
/// negative"; empty for an unbounded range.
/// @param unit The unit symbol given after the bounds, or empty.
std::string RangeRule(const FieldRange& range, std::string_view unit = {});

/// How many items a range lets a list hold: "exactly 3 items", "1 to 8 items", "at least 1
/// item", "1 or 3 items"; empty without a length.
std::string DescribeLength(const FieldRange& range);

/// A field's description followed by what its range says: its length, the rule on its values
/// (each item's for a list) and whether it is required, such as "Semi-axes in meters. Holds
/// exactly 3 items. Each must be positive. Required.".
/// @param list Whether the field is a list.
std::string FieldHelp(std::string_view description, std::string_view unit, const FieldRange& range,
                      bool list);

/// Whether a field of type `T` is a list, or an optional list.
template <typename T> struct ListFieldTrait : std::false_type {};
template <typename T> struct ListFieldTrait<std::vector<T>> : std::true_type {};
template <typename T, std::size_t N> struct ListFieldTrait<std::array<T, N>> : std::true_type {};
template <typename T> struct ListFieldTrait<std::optional<T>> : ListFieldTrait<T> {};

/// A list field, or an optional one, whose range applies to each item.
template <typename T> inline constexpr bool IsListField = ListFieldTrait<T>::value;

/// `FieldHelp` of a field of type `T`.
template <typename T>
std::string FieldHelp(std::string_view description, std::string_view unit,
                      const FieldRange& range) {
    return FieldHelp(description, unit, range, IsListField<T>);
}

/// Whether `IO` declares `static constexpr bool kBindsNative = true`.
template <typename IO, typename = void> struct BindsNativeTrait : std::false_type {};
template <typename IO>
struct BindsNativeTrait<IO, std::enable_if_t<IO::kBindsNative>> : std::true_type {};

/// An `IO`, such as the Python bindings, that takes values the configuration writes in another
/// form as they are stored, through hooks: `io.Bound(key, bound, description, unit, unset, range)`
/// for a bound unset when infinite, and `io.Group(name, group, description)` for a group of fields
/// written flat, which returns false to have them visited flat.
template <typename IO> inline constexpr bool BindsNative = BindsNativeTrait<IO>::value;

/// Reads or writes a bound that is unset when infinite, as an optional field.
/// @param unit The bound's unit symbol, or empty.
/// @param unset The bound when the field is unset: +infinity or -infinity.
template <typename IO>
void OptionalBound(IO& io, std::string_view key, double& bound, std::string_view description,
                   std::string_view unit = {},
                   double unset = std::numeric_limits<double>::infinity(),
                   const FieldRange& range = {}) {
    if constexpr (BindsNative<IO>) {
        io.Bound(key, bound, description, unit, unset, range);
        return;
    }
    std::optional<double> value;
    if (std::isfinite(bound)) {
        value = bound;
    }
    io(key, value, description, unit, range);
    bound = value.value_or(unset);
}

/// An angle in degrees as configurations give it: the shortest decimal that `RadiansFromDegrees`
/// turns back into exactly `radians`, else the nearest.
double DegreesFromRadians(double radians);

/// An angle in radians from degrees as configurations give it.
inline double RadiansFromDegrees(double degrees) {
    return degrees * kDegree;
}

/// The fields of a resolution fade (`ResolutionFade`) as "fade_px", its start and end sample
/// counts as a list of two in output samples (pixels), and "fade_shape", the curve between them.
/// @param description What the counts mean for the layer's features.
template <typename IO, typename Fade>
void FadePixelFields(IO& io, Fade& fade, std::string_view description) {
    if constexpr (BindsNative<IO>) {
        if (io.Group("fade", fade, description)) {
            return;
        }
    }
    std::array<double, 2> pixels{fade.startSamples, fade.endSamples};
    io("fade_px", pixels, description, "px", AtLeast(0.0));
    fade.startSamples = pixels[0];
    fade.endSamples = pixels[1];
    io("fade_shape", fade.shape, "Curve of the fade between the two sample counts of fade_px.");
}

/// The field "source_resolves_px": source cells across from which the source's measured data
/// already holds the layer's `features`, measured by their `size`, so only smaller ones are added.
template <typename IO>
void SourceResolvesField(IO& io, std::optional<double>& pixels, std::string_view features,
                         std::string_view size) {
    io("source_resolves_px", pixels,
       "Source cells across from which the source's data already holds the " +
           std::string(features) + ", by their " + std::string(size) +
           ": only smaller ones are added, each raster of a chain giving its own limit. "
           "Ignored on synthetic sources and where no raster has data; null adds them all.",
       "px", Above(0.0));
}

/// The keys of a crater selection's fields.
struct SelectionKeys {
    std::string_view minDiameter = "min_diameter_m";
    std::string_view maxDiameter = "max_diameter_m";
    std::string_view minAge = "min_age_gyr";
    std::string_view maxAge = "max_age_gyr";
    std::string_view manual = "manual";
};

/// The fields of a crater selection (`CraterSelection`): the diameters and ages of the craters
/// selected, and whether manual craters are.
/// @param craters What the selected craters are, such as "craters made irregular".
/// @param manualHelp The description of the manual flag.
template <typename IO, typename Selection>
void SelectionFields(IO& io, Selection& selection, std::string_view craters,
                     std::string_view manualHelp, const SelectionKeys& keys = {}) {
    if constexpr (BindsNative<IO>) {
        if (io.Group("selection", selection,
                     "The selection of " + std::string(craters) +
                         ": their diameters and ages, and whether manual craters count.")) {
            return;
        }
    }
    const std::string of = ", of " + std::string(craters);
    io(keys.minDiameter, selection.minDiameter, "Smallest diameter, in meters" + of + ".", "m",
       AtLeast(0.0));
    OptionalBound(io, keys.maxDiameter, selection.maxDiameter,
                  "Largest diameter, in meters" + of + "; unset has no upper limit.", "m",
                  std::numeric_limits<double>::infinity(), AtLeast(0.0));
    io(keys.minAge, selection.minAgeGyr, "Youngest age, in Gyr" + of + ".", "Gyr", AtLeast(0.0));
    OptionalBound(io, keys.maxAge, selection.maxAgeGyr,
                  "Oldest age, in Gyr" + of + "; unset has no upper limit.", "Gyr",
                  std::numeric_limits<double>::infinity(), AtLeast(0.0));
    io(keys.manual, selection.manual, manualHelp);
}

/// The description of a wind azimuth: where `wind`, such as "wind", blows toward, and the
/// `effect` it has, such as "slip faces face it".
inline std::string WindAzimuthHelp(std::string_view wind, std::string_view effect) {
    return "Direction the " + std::string(wind) +
           " blows toward, in degrees counter-clockwise from +x (local east on a body); " +
           std::string(effect) + ".";
}

/// The fields of the anchor of a layer laid out in a tangent frame: the longitude and latitude,
/// on a body, of the point whose tangent frame `what`, such as "the plates are laid out in".
/// @param prefix The keys' prefix, to which "_x" and "_y" are added.
template <typename IO, typename Anchor>
void AnchorFields(IO& io, Anchor& anchor, std::string_view what,
                  std::string_view prefix = "anchor") {
    if constexpr (BindsNative<IO>) {
        if (io.Group(prefix, anchor,
                     "On a body, the longitude and latitude in degrees of the point whose tangent "
                     "frame " +
                         std::string(what) + ".")) {
            return;
        }
    }
    io(std::string(prefix) + "_x", anchor.x,
       "On a body, the longitude in degrees of the point whose tangent frame " + std::string(what) +
           ".");
    io(std::string(prefix) + "_y", anchor.y, "On a body, the latitude in degrees of the anchor.");
}

/// The fields of an exposure field a layer reads on the ground beneath it, and the spacing in
/// meters it reads the ground at.
/// @param exposureHelp The description of the exposure field.
/// @param prefix The exposure's key, to which "_spacing_m" is added for the spacing's.
template <typename IO, typename Exposure>
void ExposureFields(IO& io, Exposure& exposure, double& spacing, std::string_view exposureHelp,
                    std::string_view prefix = "exposure") {
    io(std::string(prefix), exposure, exposureHelp);
    io(std::string(prefix) + "_spacing_m", spacing,
       "Spacing in meters the ground is read at for the exposure field.", "m", Above(0.0));
}

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

}  // namespace terraingen::config
