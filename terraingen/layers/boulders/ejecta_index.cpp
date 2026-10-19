#include "terraingen/layers/boulders/ejecta_index.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace terraingen {

namespace {

/// The most buckets an index holds per crater, beyond a floor for small lists.
constexpr std::size_t kBucketsPerCrater = 16;
constexpr std::size_t kMinBuckets = 1024;

}  // namespace

EjectaIndex::EjectaIndex(CraterList craters, const Surface& surface,
                         const std::function<double(const Crater&)>& reach,
                         std::vector<std::uint32_t> sources)
    : craters_(std::move(craters)), sources_(std::move(sources)) {
    const std::size_t count = craters_.Size();
    all_.resize(count);
    for (std::size_t n = 0; n < count; ++n) {
        all_[n] = static_cast<std::uint32_t>(n);
    }
    if (surface.IsBody() || count == 0) {
        return;
    }
    std::vector<Bounds2d> boxes(count);
    double minX = std::numeric_limits<double>::infinity();
    double minY = minX;
    double maxX = -minX;
    double maxY = -minX;
    double sum = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        const Crater crater = craters_.At(n);
        const double r = std::fmax(reach(crater), 0.0);
        boxes[n] = {crater.x - r, crater.y - r, crater.x + r, crater.y + r};
        minX = std::fmin(minX, boxes[n].minX);
        minY = std::fmin(minY, boxes[n].minY);
        maxX = std::fmax(maxX, boxes[n].maxX);
        maxY = std::fmax(maxY, boxes[n].maxY);
        sum += r;
    }
    const double width = maxX - minX;
    const double height = maxY - minY;
    const double budget = static_cast<double>(std::max(kMinBuckets, kBucketsPerCrater * count));
    double cell =
        std::fmax(2.0 * sum / static_cast<double>(count), std::sqrt(width * height / budget));
    if (!(cell > 0.0) || !std::isfinite(cell)) {
        cell = std::fmax(std::fmax(width, height), 1.0);
    }
    minX_ = minX;
    minY_ = minY;
    cell_ = cell;
    columns_ = static_cast<std::size_t>(std::floor(width / cell)) + 1;
    rows_ = static_cast<std::size_t>(std::floor(height / cell)) + 1;
    buckets_.resize(columns_ * rows_);
    const auto column = [&](double x) {
        return std::min(columns_ - 1,
                        static_cast<std::size_t>(std::fmax(0.0, std::floor((x - minX_) / cell_))));
    };
    const auto row = [&](double y) {
        return std::min(rows_ - 1,
                        static_cast<std::size_t>(std::fmax(0.0, std::floor((y - minY_) / cell_))));
    };
    for (std::size_t n = 0; n < count; ++n) {
        for (std::size_t j = row(boxes[n].minY); j <= row(boxes[n].maxY); ++j) {
            for (std::size_t i = column(boxes[n].minX); i <= column(boxes[n].maxX); ++i) {
                buckets_[j * columns_ + i].push_back(static_cast<std::uint32_t>(n));
            }
        }
    }
}

const std::vector<std::uint32_t>& EjectaIndex::Near(double x, double y) const {
    static const std::vector<std::uint32_t> none;
    if (buckets_.empty()) {
        return all_;
    }
    const double u = std::floor((x - minX_) / cell_);
    const double v = std::floor((y - minY_) / cell_);
    if (!(u >= 0.0 && v >= 0.0 && u < static_cast<double>(columns_) &&
          v < static_cast<double>(rows_))) {
        return none;
    }
    return buckets_[static_cast<std::size_t>(v) * columns_ + static_cast<std::size_t>(u)];
}

}  // namespace terraingen
