/// @file
/// @brief `Crater` and `CraterList`, and `CraterSelection`, which of them a model applies to.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <vector>

namespace terraingen {

/// One crater: centre in surface coordinates, rim-to-rim diameter, age and a stable ID.
struct Crater {
    double x = 0.0;         ///< Surface x of the centre.
    double y = 0.0;         ///< Surface y of the centre.
    double diameter = 0.0;  ///< Rim-to-rim diameter (m).
    double ageGyr = 0.0;    ///< Age (Gyr).
    std::uint64_t id = 0;   ///< Stable ID.
    /// Placed by hand, identified in imagery or read from a catalog rather than generated; takes
    /// precedence over generated craters and is never removed by spacing.
    bool manual = false;

    bool operator==(const Crater&) const = default;
};

/// A stable ID for a manual crater that has none, hashed with `Philox4x32`: the same for the same
/// crater in every run.
/// @param diameter In metres.
std::uint64_t ManualCraterId(double x, double y, double diameter);

/// Craters stored as one array per field, so each column can be shared with NumPy or a GPU
/// without copying.
class CraterList {
  public:
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

    /// Surface x of every centre.
    const std::vector<double>& X() const {
        return x_;
    }

    /// Surface y of every centre.
    const std::vector<double>& Y() const {
        return y_;
    }

    /// Every diameter, in metres.
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

  private:
    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> diameter_;
    std::vector<double> ageGyr_;
    std::vector<std::uint64_t> id_;
    std::vector<std::uint8_t> manual_;
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
/// positive and finite, or a negative or non-finite age, whether `keep` accepts it or not.
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

    bool operator==(const CraterSelection&) const = default;
};

/// Checks a crater selection.
/// @param what What the selection is for, which starts the error message.
/// @throws std::invalid_argument "<what> size and age ranges must satisfy 0 <= min <= max"
/// otherwise.
void ValidateCraterSelection(const CraterSelection& selection, std::string_view what);

}  // namespace terraingen
