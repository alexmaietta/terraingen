/// @file
/// @brief `CraterSelection`, which craters a crater model applies to.
#pragma once

#include "terraingen/layers/craters/population.h"

#include <limits>
#include <string_view>

namespace terraingen {

/// @brief Which craters a model applies to, by diameter, age and whether manual.
struct CraterSelection {
    double minDiameter = 0.0;                                      ///< Smallest diameter (m).
    double maxDiameter = std::numeric_limits<double>::infinity();  ///< Largest diameter (m).
    double minAgeGyr = 0.0;                                        ///< Youngest age (Gyr).
    double maxAgeGyr = std::numeric_limits<double>::infinity();    ///< Oldest age (Gyr).
    bool manual = true;  ///< Whether manual craters are included.

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
