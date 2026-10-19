/// @file
/// @brief `PreparedCrater`, a crater with everything needed to shape it at one sample spacing.
#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/irregularity.h"

#include <functional>
#include <optional>
#include <vector>

namespace terraingen {

/// A crater's datum: the plane h = `a` + `b` dx + `c` dy, dx and dy in metres east and
/// north of its centre.
struct Plane {
    double a = 0.0;  ///< Height at the centre (m).
    double b = 0.0;  ///< Slope eastward (m/m).
    double c = 0.0;  ///< Slope northward (m/m).

    /// The height a + b dx + c dy at `dx` east and `dy` north of the centre (m).
    double At(double dx, double dy) const {
        return a + b * dx + c * dy;
    }

    bool operator==(const Plane&) const = default;
};

/// A crater ready to shape at one sample spacing.
struct PreparedCrater {
    Crater crater;                 ///< The crater.
    double resolution = 1.0;       ///< `ResolutionWeight` at the sample spacing.
    CraterState state;             ///< Its degradation state.
    CraterOutline outline{};       ///< Its outline.
    std::optional<Plane> datum{};  ///< From `FitCraterDatum`; none without a datum.
    /// Its asymmetry on the ground beneath (`ShapeOnSlopes`); level without a slope response.
    CraterSlopeShape slope{};
};

/// The craters with a non-zero resolution weight at `spacing`, prepared in `CraterApplyOrder`
/// without datums; `reshape`, when set, changes each outline after `IrregularOutline`.
/// @param spacing Sample spacing in metres; positive.
/// @throws std::domain_error as `DegradationModel::State`.
std::vector<PreparedCrater>
PrepareCraters(const CraterList& craters, const CraterApplyOptions& options, double spacing,
               const DegradationModel& degradation,
               const std::function<void(const Crater&, CraterOutline&)>& reshape = {});

}  // namespace terraingen
