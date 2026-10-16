#include "terraingen/layers/detail.h"

#include "terraingen/sources/flat_source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {

/// A source of `height` everywhere, or no data at x < 0 when `holed`.
class Level final : public terraingen::Source {
  public:
    Level(float height, bool holed) : height_(height), holed_(holed) {}
    terraingen::RasterGrid Grid() const override {
        return terraingen::FlatSource(4, 4, 1.0).Grid();
    }
    void Fill(terraingen::Raster<float>& heights) const override {
        for (std::size_t i = 0; i < heights.Size(); ++i) {
            const auto p = heights.CellCenter(i % heights.Width(), i / heights.Width());
            heights.Values()[i] = HeightAt(p.x, p.y);
        }
    }
    float HeightAt(double x, double /*y*/) const override {
        return holed_ && x < 0.0 ? std::numeric_limits<float>::quiet_NaN() : height_;
    }

  private:
    float height_;
    bool holed_;
};

}  // namespace

TEST(DetailLayer, AddsTheDetailOfOneSourceAboveAnother) {
    const auto full = std::make_shared<Level>(7.0f, false);
    const auto smooth = std::make_shared<Level>(4.0f, true);
    const terraingen::DetailLayer layer(full, smooth, 0.5);
    EXPECT_EQ(layer.ApplyAt(10.0f, 1.0, 0.0, {1.0}), 11.5f);
    EXPECT_EQ(layer.ApplyAt(10.0f, -1.0, 0.0, {1.0}), 10.0f) << "no data in a source leaves it";
    EXPECT_THROW(terraingen::DetailLayer(nullptr, smooth), std::invalid_argument);
    EXPECT_THROW(terraingen::DetailLayer(full, smooth, std::nan("")), std::invalid_argument);
}
