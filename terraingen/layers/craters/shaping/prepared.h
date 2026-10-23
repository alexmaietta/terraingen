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

/// A crater's datum: the plane h = `a` + `b` dx + `c` dy, dx and dy in meters east and
/// north of its center.
struct Plane {
    double a = 0.0;  ///< Height at the center (m).
    double b = 0.0;  ///< Slope eastward (m/m).
    double c = 0.0;  ///< Slope northward (m/m).

    /// The height a + b dx + c dy at `dx` east and `dy` north of the center (m).
    double At(double dx, double dy) const {
        return a + b * dx + c * dy;
    }

    /// Memberwise equality.
    bool operator==(const Plane& other) const {
        return a == other.a && b == other.b && c == other.c;
    }
    bool operator!=(const Plane& other) const {
        return !(*this == other);
    }
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

struct CraterShapeModels;

/// The craters with a non-zero resolution weight at `spacing`, prepared in `CraterApplyOrder`
/// without datums, each in its `ResolvedCraterState`; `reshape`, when set, changes each outline
/// after `IrregularOutline`.
/// @param spacing Sample spacing in meters; positive.
/// @param models Neither may be null.
/// @throws std::domain_error as `DegradationModel::State`.
/// @throws std::invalid_argument as `ResolvedCraterState`.
std::vector<PreparedCrater>
PrepareCraters(const CraterList& craters, const CraterApplyOptions& options, double spacing,
               const CraterShapeModels& models,
               const std::function<void(const Crater&, CraterOutline&)>& reshape = {});

}  // namespace terraingen
