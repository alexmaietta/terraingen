/// @file
/// @brief `Crater` and `CraterList`, the craters every crater model works on, and
/// `CraterSelection`, which of them a model applies to.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <vector>

namespace terraingen {

/// @brief One crater: centre in surface coordinates, rim-to-rim diameter, age and a stable ID.
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

/// @brief A stable ID for a manual crater that has none, hashed with `Philox4x32`.
/// @param x Surface x of the centre.
/// @param y Surface y of the centre.
/// @param diameter Diameter, in metres.
/// @return The same ID for the same crater in every run.
std::uint64_t ManualCraterId(double x, double y, double diameter);

/// @brief Craters stored as one array per field, so each column can be shared with NumPy or a GPU
/// without copying.
class CraterList {
  public:
    /// @brief The number of craters.
    /// @return The size.
    std::size_t Size() const {
        return id_.size();
    }

    /// @brief Whether the list holds no crater.
    /// @return True when empty.
    bool Empty() const {
        return id_.empty();
    }

    /// @brief Reserves room in every column.
    /// @param count Craters to make room for.
    void Reserve(std::size_t count);

    /// @brief Appends a crater.
    /// @param crater The crater.
    void Append(const Crater& crater);

    /// @brief Appends every crater of another list, in its order.
    /// @param other The list.
    void Append(const CraterList& other);

    /// @brief The crater at an index.
    /// @param index The index.
    /// @return The crater.
    /// @throws std::out_of_range if `index` >= `Size()`.
    Crater At(std::size_t index) const;

    /// @brief The x column.
    /// @return Surface x of every centre.
    const std::vector<double>& X() const {
        return x_;
    }

    /// @brief The y column.
    /// @return Surface y of every centre.
    const std::vector<double>& Y() const {
        return y_;
    }

    /// @brief The diameter column.
    /// @return Every diameter, in metres.
    const std::vector<double>& Diameters() const {
        return diameter_;
    }

    /// @brief The age column.
    /// @return Every age, in Gyr.
    const std::vector<double>& AgesGyr() const {
        return ageGyr_;
    }

    /// @brief The ID column.
    /// @return Every ID.
    const std::vector<std::uint64_t>& Ids() const {
        return id_;
    }

    /// @brief The manual column, as bytes so it can be shared without copying.
    /// @return 1 for manual craters, 0 for generated ones.
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

/// @brief The craters a predicate keeps, in their order.
/// @param craters The craters.
/// @param keep `keep(index, crater)` returning whether to keep the crater at `index`.
/// @return The craters kept.
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

/// @brief Appends manual craters, flagged manual and given a `ManualCraterId` where their ID is 0.
/// @param manual The manual craters.
/// @param craters The list appended to.
/// @param keep Which craters to append; empty for all.
/// @throws std::invalid_argument for a crater with a non-finite position, a diameter that is not
/// positive and finite, or a negative or non-finite age, whether `keep` accepts it or not.
void AppendManualCraters(const CraterList& manual, CraterList& craters,
                         const std::function<bool(const Crater&)>& keep = {});

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
