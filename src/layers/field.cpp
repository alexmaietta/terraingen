// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/field.h"

#include "terraingen/raster/geotiff/geotiff.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terraingen {

MinimumField::MinimumField(std::shared_ptr<const SurfaceField> a,
                           std::shared_ptr<const SurfaceField> b)
    : m_a(std::move(a)), m_b(std::move(b)) {
    if (!m_a || !m_b) {
        throw std::invalid_argument("A minimum field cannot take a null field");
    }
}

double MinimumField::ValueAt(double x, double y) const {
    return std::fmin(m_a->ValueAt(x, y), m_b->ValueAt(x, y));
}

double MinimumField::Max() const {
    return std::fmax(m_a->Max(), m_b->Max());
}

void ValidateRasterFieldOptions(const RasterFieldOptions& options) {
    if (options.method != ResampleMethod::Nearest && options.method != ResampleMethod::Bilinear) {
        throw std::invalid_argument(
            "A raster field reads nearest or bilinear, which stay within the cells read");
    }
    RequirePositive(options.scale, "Raster field scale");
}

RasterField::RasterField(Raster<float> raster, RasterFieldOptions options)
    : m_raster(std::move(raster)), m_options(options) {
    ValidateRasterFieldOptions(m_options);
    if (m_raster.Empty()) {
        throw std::invalid_argument("A raster field needs a raster");
    }
    const GeoTransform& transform = m_raster.Transform();
    if (transform.IsRotated()) {
        throw std::invalid_argument("A raster field cannot read a rotated raster");
    }
    for (const float value : m_raster.Values()) {
        if (std::isfinite(value) && !m_raster.IsNoData(value) &&
            !(static_cast<double>(value) * m_options.scale <= m_max)) {
            m_max = static_cast<double>(value) * m_options.scale;
        }
    }
    if (std::isfinite(m_options.fallback) && !(m_options.fallback <= m_max)) {
        m_max = m_options.fallback;
    }
}

double RasterField::Sample(double x, double y) const {
    const Vec2d pixel = m_raster.Transform().WorldToPixel(x, y);
    const auto width = static_cast<double>(m_raster.Width());
    const auto height = static_cast<double>(m_raster.Height());
    if (!(pixel.x >= 0.0 && pixel.y >= 0.0 && pixel.x < width && pixel.y < height)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return m_options.method == ResampleMethod::Nearest ? SampleNearest(pixel)
                                                       : SampleBilinear(pixel);
}

double RasterField::SampleNearest(const Vec2d& pixel) const {
    const auto col = static_cast<std::size_t>(pixel.x);
    const auto row = static_cast<std::size_t>(pixel.y);
    return m_raster.IsMissing(m_raster(col, row)) ? std::numeric_limits<double>::quiet_NaN()
                                                  : static_cast<double>(m_raster(col, row));
}

double RasterField::SampleBilinear(const Vec2d& pixel) const {
    const auto width = static_cast<double>(m_raster.Width());
    const auto height = static_cast<double>(m_raster.Height());
    const double u = std::clamp(pixel.x - 0.5, 0.0, width - 1.0);
    const double v = std::clamp(pixel.y - 0.5, 0.0, height - 1.0);
    const auto col = static_cast<std::size_t>(u);
    const auto row = static_cast<std::size_t>(v);
    const double tx = u - static_cast<double>(col);
    const double ty = v - static_cast<double>(row);
    const std::size_t right = std::min(col + 1, m_raster.Width() - 1);
    const std::size_t below = std::min(row + 1, m_raster.Height() - 1);
    double sum = 0.0;
    double weights = 0.0;
    for (const auto& [c, r, w] :
         {std::tuple{col, row, (1.0 - tx) * (1.0 - ty)}, std::tuple{right, row, tx * (1.0 - ty)},
          std::tuple{col, below, (1.0 - tx) * ty}, std::tuple{right, below, tx * ty}}) {
        if (w > 0.0 && !m_raster.IsMissing(m_raster(c, r))) {
            sum += w * static_cast<double>(m_raster(c, r));
            weights += w;
        }
    }
    return weights > 0.0 ? sum / weights : std::numeric_limits<double>::quiet_NaN();
}

double RasterField::ValueAt(double x, double y) const {
    double value = Sample(x, y);
    if (std::isnan(value) && m_options.periodicLongitude) {
        value = Sample(x + 360.0, y);
        if (std::isnan(value)) {
            value = Sample(x - 360.0, y);
        }
    }
    if (std::isnan(value)) {
        return m_options.fallback;
    }
    return value * m_options.scale;
}

std::shared_ptr<const RasterField> ReadRasterField(const std::filesystem::path& path,
                                                   RasterFieldOptions options, std::size_t band) {
    return std::make_shared<RasterField>(ReadGeoTiff(path, band).raster, options);
}

}  // namespace terraingen
