#pragma once

#include "ernie/raster_view.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace ernie::testing {

/// Owning north-up grid for tests; converts to the `RasterView` Ernie consumes.
struct TestRaster {
    std::size_t width = 0;
    std::size_t height = 0;
    double originX = 0.0;
    double originY = 0.0;
    double cellSize = 1.0;
    std::vector<float> values;
    std::optional<float> noData;

    TestRaster() = default;

    TestRaster(std::size_t w, std::size_t h, double x0 = 0.0, double y0 = 0.0, double cell = 1.0)
        : width(w), height(h), originX(x0), originY(y0), cellSize(cell), values(w * h, 0.0f) {}

    /// A grid of `w` x `h` cells of side `cell`, centred on the world origin.
    static TestRaster Centred(std::size_t w, std::size_t h, double cell = 1.0) {
        return TestRaster(w, h, -0.5 * static_cast<double>(w) * cell,
                          0.5 * static_cast<double>(h) * cell, cell);
    }

    float& operator()(std::size_t col, std::size_t row) {
        return values[row * width + col];
    }

    float operator()(std::size_t col, std::size_t row) const {
        return values[row * width + col];
    }

    glm::dvec2 CellCenter(std::size_t col, std::size_t row) const {
        return {originX + (static_cast<double>(col) + 0.5) * cellSize,
                originY - (static_cast<double>(row) + 0.5) * cellSize};
    }

    RasterView View() const {
        return {values.empty() ? nullptr : values.data(),
                width,
                height,
                0,
                originX,
                originY,
                cellSize,
                noData};
    }

    operator RasterView() const {
        return View();
    }
};

/// A centred grid whose heights come from `height(x, y)` at each cell centre.
template <typename Function>
TestRaster Sampled(std::size_t w, std::size_t h, double cell, Function height) {
    TestRaster raster = TestRaster::Centred(w, h, cell);
    for (std::size_t row = 0; row < h; ++row) {
        for (std::size_t col = 0; col < w; ++col) {
            const glm::dvec2 c = raster.CellCenter(col, row);
            raster(col, row) = static_cast<float>(height(c.x, c.y));
        }
    }
    return raster;
}

/// Deterministic rolling terrain with relief from hundreds of metres down to a few cells, in place
/// of a noise generator: about +/-50 m of height.
inline TestRaster RollingTerrain(std::size_t w, std::size_t h, double cell = 2.0) {
    return Sampled(w, h, cell, [](double x, double y) {
        return 30.0 * std::sin(x / 90.0 + 0.3) * std::cos(y / 110.0 - 0.7) +
               12.0 * std::sin((x + y) / 41.0 + 1.1) + 5.0 * std::sin(x / 17.0 - y / 23.0) +
               2.0 * std::sin(x / 7.3) * std::cos(y / 6.1);
    });
}

}  // namespace ernie::testing
