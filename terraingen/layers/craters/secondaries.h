/// @file
/// @brief Secondary craters around the craters of a `CraterLayer`, and the size and range laws
/// they follow.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/layer.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace terraingen {

/// @brief The largest secondary crater at a range from a primary, after the generalized fit of
/// Singer et al. (2020): d = a R^-b with a = 0.022 D^1.96 and b = 0.14 ln D, D and R in km.
/// @param diameter The primary's diameter, in metres.
/// @param range The distance from the primary's centre, in metres.
/// @return The largest secondary's diameter, in metres.
double SingerMaxSecondaryDiameter(double diameter, double range);

/// @brief The range beyond which `SingerMaxSecondaryDiameter` falls below a size: its inverse in
/// range.
/// @param diameter The primary's diameter, in metres.
/// @param size The secondary's diameter, in metres.
/// @return The range, in metres; infinite for primaries of 1 km or less, whose largest secondary
/// does not shrink with range.
double SingerRangeForSize(double diameter, double size);

/// @brief How far from its centre a primary throws its first secondaries (Singer et al. 2020).
/// @param diameter The primary's diameter, in metres.
/// @return The radius plus about 1.8 diameters up to 3 km and 1.3 from 31 km, in metres.
double SecondaryOnsetRange(double diameter);

/// @brief The median ray length of Elliott et al. (2018, eq. 2): `coefficient` R^`exponent` km
/// for a crater of radius R km.
/// @param diameter The crater's diameter, in metres.
/// @param coefficient The law's coefficient, in km.
/// @param exponent The law's exponent.
/// @return The median ray length, in metres.
double ElliottMedianRayLength(double diameter, double coefficient = 6.59, double exponent = 1.27);

/// @brief Which primaries throw secondaries, how many, and over what sizes.
///
/// At range R from a primary of diameter D, secondaries at least d across number
/// n(R) (d / (`countFraction` D))^-`sizeSlope` per m^2, below `SingerMaxSecondaryDiameter` and
/// between `SecondaryOnsetRange` and the outer range.
struct SecondaryCraterOptions {
    std::uint32_t seed = 0;  ///< Varies the secondaries.
    /// Secondaries at least `countFraction` D across in an area the size of the primary at the
    /// onset; required, as the sources fix sizes but not numbers.
    std::optional<double> onsetCount;
    double countFraction = 0.01;  ///< Reference size of `onsetCount`, over D.
    double sizeSlope = 4.0;       ///< Cumulative size slope, positive.
    double minDiameter = 1.0;     ///< Smallest secondary generated (m).
    double maxDiameter = std::numeric_limits<double>::infinity();  ///< Largest (m), exclusive.
    /// Outer range in primary diameters from the centre; unset uses 1.6 times Elliott et al.'s
    /// median ray length (`ElliottMedianRayLength`).
    std::optional<double> outerRange;
    /// The primaries that throw secondaries.
    CraterSelection selection{};
    /// Polar cell side as a multiple of each size band's largest diameter.
    double cellSizeFactor = 16.0;
    /// Most secondaries expected over one region, to catch requests that would exhaust memory.
    double maxExpected = 5e7;

    bool operator==(const SecondaryCraterOptions&) const = default;
};

/// @brief Checks secondary crater options.
/// @param options The options to check.
/// @throws std::invalid_argument without a positive `onsetCount`, for a non-positive count
/// fraction or size slope, an empty or negative diameter range, a non-positive outer range, an
/// invalid selection, or a non-positive cell size factor or limit.
void ValidateSecondaryCraterOptions(const SecondaryCraterOptions& options);

/// @brief Secondary craters around the craters of a `CraterLayer`, applied additively with
/// `ApplyCraters`.
///
/// Drawn per primary in polar cells keyed to its ID, size band, ring and sector, so a tile holds
/// the same secondaries as any larger grid; bands too small for the spacing are skipped.
class SecondaryCraterLayer final : public Layer {
  public:
    /// @brief Builds a secondary crater layer.
    /// @param primaries The layer whose craters throw secondaries.
    /// @param models The profile and degradation secondaries are shaped with.
    /// @param options Which primaries throw secondaries, and how many.
    /// @param apply How secondaries are applied; without a datum.
    /// @throws std::invalid_argument for a null layer or model, invalid options, or apply options
    /// with a datum.
    SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries, CraterShapeModels models,
                         SecondaryCraterOptions options, CraterApplyOptions apply = {});

    /// @brief The largest secondary the layer places.
    /// @return Its diameter, in metres.
    double MaxFeatureDiameter() const override;

    /// @brief The secondaries a grid covering a region gets: those centred in it or within the
    /// profile's reach of it.
    /// @param region The region, in surface coordinates.
    /// @param spacing The sample spacing; 0 for every size.
    /// @return The secondaries.
    /// @throws std::invalid_argument if more than `maxExpected` are expected.
    CraterList Secondaries(const Bounds2d& region, double spacing = 0.0) const;

    /// @brief The outer range of a primary's secondaries.
    /// @param diameter The primary's diameter, in metres.
    /// @return The range from its centre, in metres.
    double OuterRange(double diameter) const;

    /// A secondary's attributes in the state the degradation model gives it.
    /// @throws std::domain_error as `DegradationModel::State`.
    CraterAttributes Attributes(const Crater& crater) const;

    /// @brief The secondary crater options.
    /// @return The options the layer was made with.
    const SecondaryCraterOptions& Options() const {
        return options_;
    }

  protected:
    /// Applies the secondaries reaching the samples.
    /// @throws std::invalid_argument if more than `maxExpected` secondaries are expected.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    std::shared_ptr<const CraterLayer> primaries_;
    CraterShapeModels models_;
    SecondaryCraterOptions options_;
    CraterApplyOptions apply_;
};

}  // namespace terraingen
