/// @file
/// @brief `SyntheticSource`, heights generated from a stack of noise layers over an unbounded
/// plane or a whole body.
#pragma once

#include "terraingen/sources/source.h"
#include "terraingen/sources/synthetic_terrain.h"
#include "terraingen/sources/upsampling.h"
#include "terraingen/util/surface.h"

#include <memory>
#include <optional>

namespace terraingen {

/// Heights generated rather than loaded: `elevation` plus a stack of noise layers
/// (`NoiseLayersAt`, measured from the origin on a plane, in 3D on a body), with no bounds.
///
/// Without a `pixelSize` every point gets the noise evaluated there, at any spacing, and the
/// source resolves everything (`ResolutionAt` is infinite). With one, the noise is sampled only
/// at posts `pixelSize` apart, one at the origin, and the upsampler refines them, as for a DEM.
/// An empty stack is flat at `elevation`.
class SyntheticSource final : public Source {
  public:
    using Source::Fill;

    /// @param params The noise stack, 0 to `kMaxNoiseLayers` layers (`ValidateNoiseLayer` each).
    /// @param elevation Meters added everywhere; finite.
    /// @param pixelSize Spacing of the posts in the surface's units (meters on a plane, degrees
    /// on a body); positive and finite, or unset to evaluate the noise at every point.
    /// @param upsampler How the posts are refined; null for cubic interpolation. Needs a
    /// `pixelSize`.
    /// @throws std::invalid_argument for too many or invalid layers, a non-finite `elevation`, a
    /// `pixelSize` that is not positive and finite, an upsampler without a `pixelSize`, or as
    /// `Upsampler::FitTo`.
    explicit SyntheticSource(SyntheticTerrainParams params = SyntheticTerrainParams{{}},
                             float elevation = 0.0f, std::optional<double> pixelSize = std::nullopt,
                             std::shared_ptr<const Upsampler> upsampler = nullptr,
                             Surface surface = {});

    SyntheticSource(const SyntheticSource&) = delete;
    SyntheticSource& operator=(const SyntheticSource&) = delete;

    /// A grid without cells: the source has no bounds. Its transform places the posts, post
    /// (0, 0) at the origin, or is a unit north-up transform at the origin without a `pixelSize`.
    RasterGrid Grid() const override;

    /// The elevation plus the noise at each cell's center, or the posts upsampled at `spacing`.
    void Fill(Raster<float>& heights, double spacing) const override;

    /// The elevation plus the noise at (`x`, `y`), or the posts upsampled at `spacing`.
    float HeightAt(double x, double y, double spacing) const override;

    /// The pixel size, or infinity without one.
    double ResolutionAt(double x, double y) const override;

    /// The noise stack.
    const SyntheticTerrainParams& Params() const {
        return params_;
    }

    /// Meters added everywhere.
    float Elevation() const {
        return elevation_;
    }

    /// The spacing of the posts, if the noise is sampled at posts.
    const std::optional<double>& PixelSize() const {
        return pixelSize_;
    }

    /// The upsampler in use, fitted to the posts; null without a pixel size.
    const std::shared_ptr<const Upsampler>& UpsamplerInUse() const {
        return upsampler_;
    }

  private:
    /// The posts: the noise sampled `pixelSize` apart.
    class NoisePosts final : public PostLattice {
      public:
        NoisePosts(const SyntheticSource& source, double pixelSize);
        GeoTransform Transform() const override {
            return transform_;
        }
        void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
                  Span<float> heights) const override;

      private:
        const SyntheticSource& source_;
        GeoTransform transform_;
    };

    /// The elevation plus the noise at (`x`, `y`).
    float NoiseAt(double x, double y) const;

    SyntheticTerrainParams params_;
    float elevation_;
    std::optional<double> pixelSize_;
    Surface surface_;
    std::optional<NoisePosts> posts_;
    std::shared_ptr<const Upsampler> upsampler_;
};

}  // namespace terraingen
