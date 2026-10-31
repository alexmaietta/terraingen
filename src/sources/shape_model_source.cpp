// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/shape_model_source.h"

#include "geometry/cube_faces.h"
#include "sources/lattice.h"
#include "terraingen/geometry/radial_shape.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

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

/// Where a direction lies on a face: its equiangular coordinates `s` and `t` and the face's weight
/// there.
struct FacePoint {
    double s = 0.0;
    double t = 0.0;
    double weight = 0.0;
};

/// Where unit `direction` lies on ICQ face `face` of `q` intervals per edge, if the face weighs it.
std::optional<FacePoint> OnFace(const Vec3d& direction, std::size_t face, double q) {
    const detail::CubeFace& f = detail::kIcqFaces[face];
    const double along = Dot(direction, f.normal);
    if (along <= 0.0) {
        return std::nullopt;
    }
    FacePoint at;
    at.s = detail::EquiangularCoordinate(Dot(direction, f.u) / along, q);
    at.t = detail::EquiangularCoordinate(Dot(direction, f.v) / along, q);
    at.weight = EdgeWeight(at.s, q) * EdgeWeight(at.t, q);
    return at.weight > 0.0 ? std::optional<FacePoint>(at) : std::nullopt;
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

void ShapeModelSource::FillSamples(SampleBlock& samples, double spacing, unsigned threads) const {
    detail::FillByTiles(samples, threads, [&](Span<const Vec2d> points, Span<float> heights) {
        FillPoints(points, heights, spacing);
    });
}

void ShapeModelSource::FillPoints(Span<const Vec2d> points, Span<float> heights,
                                  double spacing) const {
    const RadialShape& shape = *body_.Shape();
    const auto q = static_cast<double>(shape.Q());
    std::vector<Vec3d> directions(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        directions[i] = UnitVector(points[i].x, points[i].y);
    }
    std::vector<double> weighted(points.size(), 0.0);
    std::vector<double> total(points.size(), 0.0);
    std::vector<double> weights(points.size(), 0.0);
    for (std::size_t face = 0; face < detail::kIcqFaces.size(); ++face) {
        detail::EvaluateChosen(
            points.size(),
            [&](std::size_t i) -> std::optional<Vec2d> {
                const std::optional<FacePoint> at = OnFace(directions[i], face, q);
                if (!at) {
                    return std::nullopt;
                }
                weights[i] = at->weight;
                const double column =
                    at->s + static_cast<double>(face) * static_cast<double>(kFaceColumns);
                return Vec2d{column * labelDegrees_, -at->t * labelDegrees_};
            },
            [&](Span<const Vec2d> onFace, Span<float> values) {
                upsamplers_[face]->HeightsAt(*posts_[face], onFace, values, spacing);
            },
            [&](std::size_t i, float value) {
                weighted[i] += weights[i] * static_cast<double>(value);
                total[i] += weights[i];
            });
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[i] =
            static_cast<float>(weighted[i] / total[i] + reference_ - shape.At(directions[i]));
    }
}

double ShapeModelSource::ResolutionAt(double /*x*/, double /*y*/) const {
    return labelDegrees_;
}

double ShapeModelSource::PostSpacingAt(double /*x*/, double /*y*/) const {
    return labelDegrees_;
}

}  // namespace terraingen
