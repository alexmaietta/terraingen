/// @file
/// @brief `PreparedCrater`, a crater with everything needed to shape it at one sample spacing.
#pragma once

#include "terraingen/layers/craters/apply_options.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/irregularity.h"
#include "terraingen/layers/craters/population.h"

#include <optional>
#include <vector>

namespace terraingen {

/// A crater's datum: the plane h = `a` + `b` dx + `c` dy in metres east (dx) and north (dy) of its
/// centre.
struct Plane {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;

    /// @brief The plane's height at an offset from the crater's centre.
    /// @param dx East offset, in metres.
    /// @param dy North offset, in metres.
    /// @return a + b dx + c dy.
    double At(double dx, double dy) const {
        return a + b * dx + c * dy;
    }

    bool operator==(const Plane&) const = default;
};

/// A crater ready to shape: its weight at the sample spacing, its degradation state, its outline
/// and, once fitted, its datum.
struct PreparedCrater {
    Crater crater;
    double resolution = 1.0;  ///< `ResolutionWeight` at the sample spacing.
    CraterState state;
    CraterOutline outline{};
    std::optional<Plane> datum{};  ///< From `FitCraterDatum`; none without a datum.
};

/// @brief Prepares the craters a sample spacing resolves, in the order they are applied.
/// @param craters The craters.
/// @param options Their order, resolution fade and irregularity.
/// @param spacing The sample spacing, in metres; positive.
/// @param degradation Gives each crater's state.
/// @return The craters with a non-zero resolution weight, in `CraterApplyOrder`, without datums.
std::vector<PreparedCrater> PrepareCraters(const CraterList& craters,
                                           const CraterApplyOptions& options, double spacing,
                                           const DegradationModel& degradation);

}  // namespace terraingen
