// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Crater` and `CraterList`, and `CraterSelection`, which of them a model applies to.
#pragma once

#include "terraingen/noise/hash.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <vector>

namespace terraingen {

/// Whether two values are equal or both NaN.
inline bool SameOrBothNan(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}

/// One crater: center in surface coordinates, rim-to-rim diameter, age and a stable ID, and for a
/// manual crater optionally its depth and rim height.
struct Crater {
    double x = 0.0;         ///< Surface x of the center.
    double y = 0.0;         ///< Surface y of the center.
    double diameter = 0.0;  ///< Rim-to-rim diameter (m).
    double ageGyr = 0.0;    ///< Age (Gyr).
    std::uint64_t id = 0;   ///< Stable ID.
    /// Placed by hand, identified in imagery or read from a catalog rather than generated; takes
    /// precedence over generated craters and is never removed by spacing.
    bool manual = false;
    /// Rim-to-floor depth (m) of a manual crater; NaN takes it from the degradation model.
    double depth = std::numeric_limits<double>::quiet_NaN();
    /// Rim crest above the pre-impact surface (m) of a manual crater with a `depth`; NaN derives
    /// it from the depth (`ResolvedCraterState`).
    double rimHeight = std::numeric_limits<double>::quiet_NaN();

    /// Sets the age in Gyr, for chaining after `ManualCrater`.
    Crater& AgeGyr(double gyr) {
        ageGyr = gyr;
        return *this;
    }
    /// Sets the rim-to-floor depth in meters.
    Crater& Depth(double meters) {
        depth = meters;
        return *this;
    }
    /// Sets the rim crest above the pre-impact surface in meters; needs a `depth`.
    Crater& RimHeight(double meters) {
        rimHeight = meters;
        return *this;
    }

    /// Memberwise equality, with NaN depths and rim heights equal to each other.
    bool operator==(const Crater& other) const {
        return x == other.x && y == other.y && diameter == other.diameter &&
               ageGyr == other.ageGyr && id == other.id && manual == other.manual &&
               SameOrBothNan(depth, other.depth) && SameOrBothNan(rimHeight, other.rimHeight);
    }
    bool operator!=(const Crater& other) const {
        return !(*this == other);
    }
};

/// A stable ID for a manual crater that has none, hashed with `Philox4x32`: the same for the same
/// crater in every run.
/// @param diameter In meters.
std::uint64_t ManualCraterId(double x, double y, double diameter);

/// A fresh crater placed by hand at (`x`, `y`), `diameter` meters across, flagged `manual`, with
/// its ID left 0 for the layer to hash; set the rest by name:
///
///     ManualCrater(-4.0, 3.0, 9.0).AgeGyr(0.5).Depth(1.8)
inline Crater ManualCrater(double x, double y, double diameter) {
    Crater crater;
    crater.x = x;
    crater.y = y;
    crater.diameter = diameter;
    crater.manual = true;
    return crater;
}

/// The 32-bit key that seeds crater `id`'s noise or texture under `seed`, the same on the CPU and
/// the GPU, which is handed it.
constexpr std::uint32_t CraterKey(std::uint64_t id, std::uint32_t seed) {
    return HashWord64(id, seed);
}

/// Craters stored as one array per field, so each column can be shared with NumPy or a GPU
/// without copying.
class CraterList {
  public:
    /// An empty list.
    CraterList() = default;

    /// A list of `craters`, in order.
    CraterList(std::initializer_list<Crater> craters);

    /// The number of craters.
    std::size_t Size() const {
        return id_.size();
    }

    /// Whether the list holds no crater.
    bool Empty() const {
        return id_.empty();
    }

    /// Reserves room for `count` craters in every column.
    void Reserve(std::size_t count);

    /// Appends a crater.
    void Append(const Crater& crater);

    /// Appends every crater of `other`, in its order.
    void Append(const CraterList& other);

    /// The crater at `index`.
    /// @throws std::out_of_range if `index` >= `Size()`.
    Crater At(std::size_t index) const;

    /// Surface x of every center.
    const std::vector<double>& X() const {
        return x_;
    }

    /// Surface y of every center.
    const std::vector<double>& Y() const {
        return y_;
    }

    /// Every diameter, in meters.
    const std::vector<double>& Diameters() const {
        return diameter_;
    }

    /// Every age, in Gyr.
    const std::vector<double>& AgesGyr() const {
        return ageGyr_;
    }

    /// Every ID.
    const std::vector<std::uint64_t>& Ids() const {
        return id_;
    }

    /// 1 for manual craters, 0 for generated ones; bytes so it can be shared without copying.
    const std::vector<std::uint8_t>& Manual() const {
        return manual_;
    }

    /// Every explicit depth in meters (`Crater::depth`), NaN where it comes from the model.
    const std::vector<double>& Depths() const {
        return depth_;
    }

    /// Every explicit rim height in meters (`Crater::rimHeight`), NaN where it is derived.
    const std::vector<double>& RimHeights() const {
        return rimHeight_;
    }

  private:
    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> diameter_;
    std::vector<double> ageGyr_;
    std::vector<std::uint64_t> id_;
    std::vector<std::uint8_t> manual_;
    std::vector<double> depth_;
    std::vector<double> rimHeight_;
};

/// The craters `keep(index, crater)` keeps, in their order.
template <typename Keep> CraterList FilterCraters(const CraterList& craters, const Keep& keep) {
    CraterList kept;
    kept.Reserve(craters.Size());
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        if (keep(n, crater)) {
            kept.Append(crater);
        }
    }
    return kept;
}

/// Appends `manual` to `craters`, flagged manual and given a `ManualCraterId` where their ID is 0.
/// @param keep Which craters to append; empty for all.
/// @throws std::invalid_argument for a crater with a non-finite position, a diameter that is not
/// positive and finite, a negative or non-finite age, a depth that is neither NaN nor positive and
/// finite, a rim height that is neither NaN nor finite and not negative, or a rim height without
/// a depth, whether `keep` accepts it or not.
void AppendManualCraters(const CraterList& manual, CraterList& craters,
                         const std::function<bool(const Crater&)>& keep = {});

/// Which craters a model applies to, by diameter, age and whether manual.
struct CraterSelection {
    double minDiameter = 0.0;                                      ///< Smallest diameter (m).
    double maxDiameter = std::numeric_limits<double>::infinity();  ///< Largest diameter (m).
    double minAgeGyr = 0.0;                                        ///< Youngest age (Gyr).
    double maxAgeGyr = std::numeric_limits<double>::infinity();    ///< Oldest age (Gyr).
    bool manual = true;  ///< Whether manual craters are included.

    /// Whether a crater is selected: not for a manual crater without `manual`, or a diameter or age
    /// outside its range.
    bool Includes(const Crater& crater) const;

    /// Memberwise equality.
    bool operator==(const CraterSelection& other) const {
        return minDiameter == other.minDiameter && maxDiameter == other.maxDiameter &&
               minAgeGyr == other.minAgeGyr && maxAgeGyr == other.maxAgeGyr &&
               manual == other.manual;
    }
    bool operator!=(const CraterSelection& other) const {
        return !(*this == other);
    }
};

/// Checks a crater selection.
/// @param what What the selection is for, which starts the error message.
/// @throws std::invalid_argument "<what> size and age ranges must satisfy 0 <= min <= max"
/// otherwise.
void ValidateCraterSelection(const CraterSelection& selection, std::string_view what);

}  // namespace terraingen
