/// @file
/// @brief Parallel loops over a raster's cells: every cell, or the windows around features.
#pragma once

#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/parallel.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace terraingen {

/// A block of a raster's cells: columns [`colBegin`, `colEnd`) and rows [`rowBegin`, `rowEnd`).
struct PixelWindow {
    std::size_t colBegin = 0;
    std::size_t colEnd = 0;
    std::size_t rowBegin = 0;
    std::size_t rowEnd = 0;

    /// @brief Whether the window holds no cell.
    /// @return True when either range is empty.
    bool Empty() const {
        return colBegin >= colEnd || rowBegin >= rowEnd;
    }

    bool operator==(const PixelWindow&) const = default;
};

/// @brief The cells of a raster covering a box, clamped to the raster.
/// @param raster The raster; its transform maps pixels to the box's coordinates.
/// @param box The box, in world coordinates.
/// @return The window of pixels whose extent meets the box's pixel bounds; possibly empty.
PixelWindow WindowOf(const Raster<float>& raster, const Bounds2d& box);

/// @brief The non-empty windows of a raster covering a box on a surface.
/// @param raster The raster, in `surface`'s coordinates.
/// @param box The box: on a sphere a longitude-latitude box, which is also taken a turn either way
/// so a raster running past the antimeridian is covered on both sides.
/// @param surface What the raster lies on.
/// @return One window on a plane, up to three on a sphere; none off the raster.
std::vector<PixelWindow> WindowsAround(const Raster<float>& raster, const Bounds2d& box,
                                       const Surface& surface);

/// Rows per band of `ForEachFeatureCell`, the unit of work handed to a thread.
inline constexpr std::size_t kBandRows = 32;

/// One cell `ForEachFeatureCell` visits.
struct FeatureCell {
    std::size_t feature = 0;  ///< Index of the feature.
    std::size_t window = 0;   ///< Index of the window among the feature's.
    std::size_t col = 0;
    std::size_t row = 0;
    Vec2d centre{};  ///< World coordinates of the cell's centre.
};

/// @brief Visits the cells of each feature's windows, feature by feature, on several threads.
///
/// The raster is split into bands of `kBandRows` rows, one per task; each band visits the features
/// in order, so every cell sees the features in the same order whatever `threads` is. Cells
/// holding the no-data value are skipped.
/// @param raster The raster whose cells are visited.
/// @param threads Threads to split the bands over; 0 for every hardware thread.
/// @param featureCount The number of features.
/// @param windowsOf `windowsOf(n)` gives the windows of feature n, as a `std::vector<PixelWindow>`
/// (`WindowsAround`); windows of one feature must not overlap.
/// @param visit `visit(cell, at)` with `cell` the value to read and update and `at` its place.
template <typename WindowsOf, typename Visit>
void ForEachFeatureCell(Raster<float>& raster, unsigned threads, std::size_t featureCount,
                        const WindowsOf& windowsOf, const Visit& visit) {
    const std::size_t bands = (raster.Height() + kBandRows - 1) / kBandRows;
    ParallelFor(bands, threads, [&](std::size_t band) {
        const std::size_t bandBegin = band * kBandRows;
        const std::size_t bandEnd = std::min(raster.Height(), bandBegin + kBandRows);
        for (std::size_t n = 0; n < featureCount; ++n) {
            const std::vector<PixelWindow>& windows = windowsOf(n);
            for (std::size_t w = 0; w < windows.size(); ++w) {
                const PixelWindow& window = windows[w];
                const std::size_t rowEnd = std::min(bandEnd, window.rowEnd);
                for (std::size_t row = std::max(bandBegin, window.rowBegin); row < rowEnd; ++row) {
                    for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
                        float& cell = raster(col, row);
                        if (!raster.IsNoData(cell)) {
                            visit(cell, FeatureCell{n, w, col, row, raster.CellCenter(col, row)});
                        }
                    }
                }
            }
        }
    });
}

/// @brief Sets each cell of a raster holding data to a function of its height and centre, rows
/// over several threads.
/// @param heights The raster to update in place.
/// @param threads Threads to split the rows over; 0 for every hardware thread.
/// @param map `map(height, centre)` returning the cell's new height as a `float`.
template <typename Map> void MapCells(Raster<float>& heights, unsigned threads, const Map& map) {
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            if (!heights.IsNoData(cell)) {
                cell = map(cell, heights.CellCenter(col, row));
            }
        }
    });
}

}  // namespace terraingen
