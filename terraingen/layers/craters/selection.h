/// @file
/// @brief `CraterSelection`, which craters a crater model applies to.
#pragma once

#include "terraingen/layers/craters/population.h"

#include <limits>
#include <string_view>

namespace terraingen {

/// Which craters a model applies to: diameters (m) and ages (Gyr) within these ranges, and manual
/// craters only with `manual`.
struct CraterSelection {
    double minDiameter = 0.0;
    double maxDiameter = std::numeric_limits<double>::infinity();
    double minAgeGyr = 0.0;
    double maxAgeGyr = std::numeric_limits<double>::infinity();
    bool manual = true;

    /// @brief Whether a crater is selected.
    /// @param crater The crater.
    /// @return False for a manual crater without `manual`, or a diameter or age outside its range.
    bool Includes(const Crater& crater) const;

    bool operator==(const CraterSelection&) const = default;
};

/// @brief Checks a crater selection.
/// @param selection The selection to check.
/// @param what What the selection is for, which starts the error message.
/// @throws std::invalid_argument "<what> size and age ranges must satisfy 0 <= min <= max"
/// otherwise.
void ValidateCraterSelection(const CraterSelection& selection, std::string_view what);

}  // namespace terraingen
