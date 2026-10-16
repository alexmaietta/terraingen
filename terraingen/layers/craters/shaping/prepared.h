/// @file
/// @brief `PreparedCrater`, a crater with everything needed to shape it at one sample spacing.
#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/irregularity.h"

#include <optional>
#include <vector>

namespace terraingen {

/// @brief A crater's datum: the plane h = `a` + `b` dx + `c` dy, dx and dy in metres east and
/// north of its centre.
struct Plane {
    double a = 0.0;  ///< Height at the centre (m).
    double b = 0.0;  ///< Slope eastward (m/m).
    double c = 0.0;  ///< Slope northward (m/m).

    /// @brief The plane's height at an offset from the crater's centre.
    /// @param dx East offset, in metres.
    /// @param dy North offset, in metres.
    /// @return a + b dx + c dy.
    double At(double dx, double dy) const {
        return a + b * dx + c * dy;
    }

    bool operator==(const Plane&) const = default;
};

/// @brief A crater ready to shape at one sample spacing.
struct PreparedCrater {
    Crater crater;                 ///< The crater.
    double resolution = 1.0;       ///< `ResolutionWeight` at the sample spacing.
    CraterState state;             ///< Its degradation state.
    CraterOutline outline{};       ///< Its outline.
    std::optional<Plane> datum{};  ///< From `FitCraterDatum`; none without a datum.
};

/// @brief Prepares the craters a sample spacing resolves, in the order they are applied.
/// @param craters The craters.
/// @param options Their order, resolution fade and irregularity.
/// @param spacing The sample spacing, in metres; positive.
/// @param degradation Gives each crater's state.
/// @return The craters with a non-zero resolution weight, in `CraterApplyOrder`, without datums.
/// @throws std::domain_error as `DegradationModel::State`.
std::vector<PreparedCrater> PrepareCraters(const CraterList& craters,
                                           const CraterApplyOptions& options, double spacing,
                                           const DegradationModel& degradation);

}  // namespace terraingen
