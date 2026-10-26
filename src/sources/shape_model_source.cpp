// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/shape_model_source.h"

#include "terraingen/util/parallel.h"
#include "terraingen/util/radial_shape.h"
#include "util/cube_faces.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {
namespace {

/// Equiangular angles beyond a face's center are kept this far short of a right angle, where the
/// face's plane no longer reaches.
constexpr double kMaxFaceAngle = 0.5 * kPi - 0.01;

/// The gnomonic coordinate of equiangular coordinate `s` of a face of `q` intervals per edge,
/// beyond the face too, up to `kMaxFaceAngle` from its center.
double ExtendedGnomonic(double s, double q) {
    const double angle = (s / q - 0.5) * 0.5 * kPi;
    return std::tan(std::clamp(angle, -kMaxFaceAngle, kMaxFaceAngle));
}

/// The weight of a face at equiangular coordinate `s` of `q` intervals per edge: 1 inside, 0
/// half a post beyond either edge and a half on it.
double EdgeWeight(double s, double q) {
    return std::clamp(std::min(s, q - s) + 0.5, 0.0, 1.0);
}

}  // namespace

ShapeModelSource::FacePosts::FacePosts(const ShapeModelSource& source, std::size_t face)
    : source_(source), face_(face) {}

GeoTransform ShapeModelSource::FacePosts::Transform() const {
    const double size = source_.labelDegrees_;
    return GeoTransform::NorthUp(-0.5 * size, 0.5 * size, size);
}

std::optional<PixelWindow> ShapeModelSource::FacePosts::Extent() const {
    const std::size_t first = face_ * static_cast<std::size_t>(kFaceColumns);
    const std::size_t labels = source_.body_.Shape()->Q() + 1;
    return PixelWindow{first, first + labels, 0, labels};
}

void ShapeModelSource::FacePosts::Read(std::int64_t col0, std::int64_t row0, std::size_t width,
                                       std::size_t height, Span<float> heights) const {
    const RadialShape& shape = *source_.body_.Shape();
    const auto q = static_cast<std::int64_t>(shape.Q());
    const std::int64_t first = static_cast<std::int64_t>(face_) * kFaceColumns;
    const detail::CubeFace& face = detail::kIcqFaces[face_];
    for (std::size_t r = 0; r < height; ++r) {
        const std::int64_t j = row0 + static_cast<std::int64_t>(r);
        for (std::size_t c = 0; c < width; ++c) {
            const std::int64_t i = col0 + static_cast<std::int64_t>(c) - first;
            double radius = 0.0;
            if (i >= 0 && i <= q && j >= 0 && j <= q) {
                radius = shape.Radii()[shape.LabelIndex(static_cast<std::size_t>(i),
                                                        static_cast<std::size_t>(j), face_ + 1)];
            } else {
                const Vec3d p =
                    detail::CubeFacePoint(face, ExtendedGnomonic(static_cast<double>(i), q),
                                          ExtendedGnomonic(static_cast<double>(j), q));
                radius = shape.At(p / Length(p));
            }
            heights[r * width + c] = static_cast<float>(radius - source_.reference_);
        }
    }
}

ShapeModelSource::ShapeModelSource(Surface body, std::shared_ptr<const Upsampler> upsampler)
    : body_(std::move(body)) {
    const RadialShape* shape = body_.Shape();
    if (!shape) {
        throw std::invalid_argument("A shape-model source needs a shape-model body");
    }
    reference_ = 0.5 * (shape->MinRadius() + shape->MaxRadius());
    labelDegrees_ = 90.0 / static_cast<double>(shape->Q());
    for (std::size_t face = 0; face < posts_.size(); ++face) {
        posts_[face] = std::make_unique<FacePosts>(*this, face);
        upsamplers_[face] = FittedUpsampler(upsampler, *posts_[face]);
    }
}

RasterGrid ShapeModelSource::Grid() const {
    return {0, 0, GeoTransform::NorthUp(-180.0, 90.0, labelDegrees_)};
}

double ShapeModelSource::FaceHeight(std::size_t face, const Vec3d& direction,
                                    double spacing) const {
    const detail::CubeFace& f = detail::kIcqFaces[face];
    const double along = Dot(direction, f.normal);
    const auto q = static_cast<double>(body_.Shape()->Q());
    const double s = detail::EquiangularCoordinate(Dot(direction, f.u) / along, q);
    const double t = detail::EquiangularCoordinate(Dot(direction, f.v) / along, q);
    const double column = s + static_cast<double>(face) * static_cast<double>(kFaceColumns);
    return UpsampledHeight(*upsamplers_[face], *posts_[face], column * labelDegrees_,
                           -t * labelDegrees_, spacing);
}

float ShapeModelSource::HeightAt(double x, double y, double spacing) const {
    const RadialShape& shape = *body_.Shape();
    const Vec3d direction = UnitVector(x, y);
    const auto q = static_cast<double>(shape.Q());
    double weighted = 0.0;
    double total = 0.0;
    for (std::size_t face = 0; face < detail::kIcqFaces.size(); ++face) {
        const detail::CubeFace& f = detail::kIcqFaces[face];
        const double along = Dot(direction, f.normal);
        if (along <= 0.0) {
            continue;
        }
        const double weight =
            EdgeWeight(detail::EquiangularCoordinate(Dot(direction, f.u) / along, q), q) *
            EdgeWeight(detail::EquiangularCoordinate(Dot(direction, f.v) / along, q), q);
        if (weight > 0.0) {
            weighted += weight * FaceHeight(face, direction, spacing);
            total += weight;
        }
    }
    return static_cast<float>(weighted / total + reference_ - shape.At(direction));
}

void ShapeModelSource::Fill(Raster<float>& heights, double spacing, unsigned threads) const {
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            heights(col, row) = HeightAt(center.x, center.y, spacing);
        }
    });
    MarkMissing(heights, std::nullopt);
}

double ShapeModelSource::ResolutionAt(double /*x*/, double /*y*/) const {
    return labelDegrees_;
}

}  // namespace terraingen
