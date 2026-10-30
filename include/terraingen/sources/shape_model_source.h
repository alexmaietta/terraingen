// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ShapeModelSource`, a shape-model body's own labels refined by an upsampler.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/upsampling.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace terraingen {

/// The heights a shape model's labels give when an upsampler refines them, over the body made
/// from it (`Surface::ShapeModel`), whose radius interpolates the same labels bilinearly.
///
/// Each of the six ICQ faces is a lattice of (Q + 1)^2 posts, the label radii, in degrees: Q
/// intervals span the face's 90 degrees. Posts beyond a face's edge are read from the shape where
/// the face's lattice extends onto its neighbors, so kernel upsamplers agree along the edges. Each
/// face's posts are numbered apart from the others', so diamond-square displaces each face
/// differently, and within half a post of an edge the faces' heights are blended. A height is the
/// refined radius less the body's, 0 at every label: the shape gives the labels and the source
/// what an upsampler makes between them.
class ShapeModelSource final : public Source {
  public:
    /// @param body A shape-model body.
    /// @param upsampler How the labels are refined; null for cubic interpolation.
    /// @throws std::invalid_argument for a body without a shape, or as `Upsampler::FitTo`.
    explicit ShapeModelSource(Surface body, std::shared_ptr<const Upsampler> upsampler = nullptr);

    ShapeModelSource(const ShapeModelSource&) = delete;
    ShapeModelSource& operator=(const ShapeModelSource&) = delete;

    /// A grid without cells over the whole body, its cells the labels' spacing at a face's center.
    RasterGrid Grid() const override;

    /// The labels' spacing at a face's center, 90 / Q degrees.
    double ResolutionAt(double x, double y) const override;

    /// The labels' spacing at a face's center, 90 / Q degrees.
    double PostSpacingAt(double x, double y) const override;

    /// The body.
    const Surface& Body() const {
        return body_;
    }

    /// The upsampler of each face, fitted to its posts.
    const std::array<std::shared_ptr<const Upsampler>, 6>& UpsamplersInUse() const {
        return upsamplers_;
    }

  protected:
    /// The refined radius at each sample's longitude and latitude in degrees at a sample spacing
    /// of `spacing` degrees, less the body's radius there; each face upsamples the points it
    /// weighs at once, a grid tile by tile over threads.
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const override;

  private:
    /// `FillSamples` of `points` into `heights`.
    void FillPoints(Span<const Vec2d> points, Span<float> heights, double spacing) const;

    /// Columns between the first posts of consecutive faces, more than any face reads.
    static constexpr std::int64_t kFaceColumns = std::int64_t{1} << 20;

    /// One ICQ face's labels as posts, less the reference radius, numbered from `face` times
    /// `kFaceColumns` columns.
    class FacePosts final : public PostLattice {
      public:
        FacePosts(const ShapeModelSource& source, std::size_t face);
        GeoTransform Transform() const override;
        std::optional<PixelWindow> Extent() const override;
        void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
                  Span<float> heights) const override;

      private:
        const ShapeModelSource& source_;
        std::size_t face_;
    };

    Surface body_;
    double reference_ = 0.0;
    double labelDegrees_ = 0.0;
    std::array<std::unique_ptr<FacePosts>, 6> posts_;
    std::array<std::shared_ptr<const Upsampler>, 6> upsamplers_;
};

}  // namespace terraingen
