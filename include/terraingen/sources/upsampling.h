// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Upsamplers: how a source's posts become heights at any point and sample spacing.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/sample_block.h"
#include "terraingen/util/span.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace terraingen {

/// The posts of a source: heights on a regular lattice, such as a DEM's cells or noise sampled at
/// a fixed spacing. Implementations must be thread-safe.
class PostLattice {
  public:
    virtual ~PostLattice() = default;

    /// Where the posts lie: post (col, row) is at the center of cell (col, row) of a grid with
    /// this transform.
    virtual GeoTransform Transform() const = 0;

    /// The posts held, which an upsampler may estimate settings from; none for a lattice without
    /// bounds, which holds a post everywhere.
    virtual std::optional<PixelWindow> Extent() const {
        return std::nullopt;
    }

    /// Reads posts [`col0`, `col0` + `width`) x [`row0`, `row0` + `height`) row by row into
    /// `heights`, of `width` x `height` values, NaN where a post has no data. Posts beyond a
    /// bounded lattice repeat its nearest edge post.
    virtual void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
                      Span<float> heights) const = 0;
};

/// Where a raster that is a window of a larger one, such as a window read from a file, lies in
/// it.
struct RasterPlacement {
    RasterGrid whole{};   ///< The larger raster's grid.
    std::size_t col = 0;  ///< The window's first column in it.
    std::size_t row = 0;  ///< The window's first row in it.

    /// A raster placed in itself.
    static RasterPlacement Whole(const Raster<float>& raster) {
        return {raster.Grid(), 0, 0};
    }
};

/// The posts of a raster that is a window of a larger lattice, such as a window read from a
/// file: posts beyond the larger lattice repeat its edge, and those beyond the window its edge, so
/// reads within the window give the same values as the whole.
class RasterPosts final : public PostLattice {
  public:
    /// @param window A non-empty, unrotated raster; its no-data cells read as NaN.
    /// @param placement Where `window` lies in the whole lattice.
    /// @param wrapColumns Whether columns repeat with the lattice's width, as longitudes around a
    /// body do, rather than repeating the edge posts; `window` then spans every column.
    /// @throws std::invalid_argument for a null or empty window, or a wrapping one that does not
    /// span every column.
    RasterPosts(std::shared_ptr<const Raster<float>> window, const RasterPlacement& placement,
                bool wrapColumns = false);

    /// The whole lattice's transform.
    GeoTransform Transform() const override {
        return placement_.whole.transform;
    }

    /// The window's cells in the whole lattice.
    std::optional<PixelWindow> Extent() const override;

    void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
              Span<float> heights) const override;

  private:
    std::shared_ptr<const Raster<float>> window_;
    RasterPlacement placement_;
    bool wrapColumns_ = false;
};

/// The spacing of a lattice's posts: the longer side of a cell of `transform`, in its units.
double PostSpacing(const GeoTransform& transform);

/// Whether `spacing` is no finer than the posts of a lattice placed by `transform`, to within
/// rounding, measuring a post's cell as grids measure theirs (`RasterGrid::PixelSize`).
bool AtPostSpacing(const GeoTransform& transform, double spacing);

/// How a source's posts become heights at any point and at any sample spacing in the posts'
/// units: interpolation, or refinement that adds detail as the spacing shrinks. Heights must
/// depend only on the posts, the position and the spacing, and implementations must be
/// thread-safe.
///
/// A post keeps its own height at the posts' spacing or coarser, so an upsampler only decides
/// heights off the posts and below their spacing, in `FillSamples`, the one method it evaluates.
class Upsampler {
  public:
    virtual ~Upsampler() = default;

    /// Sets every sample of `samples` to its height from `posts` at a sample spacing of
    /// `spacing`, NaN where it has none: a post's own where the sample is centered on one and the
    /// spacing is no finer than the posts (`AtPostSpacing`), otherwise `FillSamples`'. A grid on
    /// the posts' lattice at their spacing or coarser copies them.
    /// @param threads Threads to fill a grid over; 0 for every hardware thread.
    void Fill(const PostLattice& posts, SampleBlock& samples, double spacing,
              unsigned threads) const;

    /// `Fill` of every cell of `heights`, placed by its transform.
    /// @param threads Threads to fill over; 0 for every hardware thread.
    void Fill(const PostLattice& posts, Raster<float>& heights, double spacing,
              unsigned threads) const;

    /// The height `Fill` gives a cell centered at world (`x`, `y`), to the same bits.
    float HeightAt(const PostLattice& posts, double x, double y, double spacing) const;

    /// Sets each of `heights` to `HeightAt` the world point of `points` at its index, to the same
    /// bits, in one `Fill` on one thread.
    /// @throws std::invalid_argument for spans of different sizes.
    void HeightsAt(const PostLattice& posts, Span<const Vec2d> points, Span<float> heights,
                   double spacing) const;

    /// Posts either side of a point that `FillSamples` reads, so that a source reading a window
    /// of its posts reads enough around it: 2 by default.
    virtual int Reach() const {
        return 2;
    }

    /// An upsampler for `posts` with any settings this one leaves to be estimated from them filled
    /// in, such as diamond-square's roughness; null when this one serves as it is, the default.
    virtual std::shared_ptr<const Upsampler> FitTo(const PostLattice& /*posts*/) const {
        return nullptr;
    }

    /// A copy of the upsampler whose seeds draw from `seed` (`ReseedWord`); null for one without
    /// seeds, the default, which a reseeded source keeps.
    virtual std::shared_ptr<const Upsampler> Reseeded(std::uint64_t /*seed*/) const {
        return nullptr;
    }

  protected:
    /// Sets every sample of `samples` to the height from `posts` there at a sample spacing of
    /// `spacing`, NaN where it has none, the same bits for a grid's cell as for a point at its
    /// center.
    /// @param threads Threads to split a grid over; 0 for every hardware thread.
    virtual void FillSamples(const PostLattice& posts, SampleBlock& samples, double spacing,
                             unsigned threads) const = 0;
};

/// Interpolation of the posts with a kernel (`ResampleMethod`), at every spacing alike: `Cubic`,
/// the default, gives the same bits as `SampleRaster` of the posts.
class KernelUpsampler final : public Upsampler {
  public:
    explicit KernelUpsampler(ResampleMethod method = ResampleMethod::Cubic) : method_(method) {}

    /// The kernel's radius (`KernelRadius`).
    int Reach() const override {
        return KernelRadius(method_);
    }

  protected:
    /// The posts interpolated at each sample as `SampleRaster` interpolates a raster, NaN where a
    /// post weighed holds no data. Points close together read the posts once,
    /// reusing a row's kernel weights in the rows below that share them.
    /// @throws std::invalid_argument for a rotated lattice.
    void FillSamples(const PostLattice& posts, SampleBlock& samples, double spacing,
                     unsigned threads) const override;

  private:
    ResampleMethod method_;
};

/// The upsampler sources use unless given one: cubic interpolation, shared.
std::shared_ptr<const Upsampler> DefaultUpsampler();

/// `upsampler`, or `DefaultUpsampler()` when null, fitted to `posts` (`Upsampler::FitTo`).
/// @throws std::invalid_argument as `Upsampler::FitTo`.
std::shared_ptr<const Upsampler> FittedUpsampler(std::shared_ptr<const Upsampler> upsampler,
                                                 const PostLattice& posts);

/// The built-in kernel upsamplers' names, as configurations give them: nearest, bilinear, cubic,
/// catmull_rom, lanczos3 and cubic_bspline.
const std::vector<std::string>& KernelUpsamplerNames();

/// The kernel a built-in kernel upsampler's name gives, or none for another name.
std::optional<ResampleMethod> KernelUpsamplerMethod(const std::string& name);

/// Makes an upsampler from its settings: a JSON object as text, "{}" without any.
using UpsamplerFactory =
    std::function<std::shared_ptr<const Upsampler>(const std::string& settings)>;

/// Registers an upsampler type of your own under `name`, replacing any registered under it
/// before, so that configurations can name it as `"upsampling": "name"` or
/// `{"type": "name", ...settings}`.
/// @throws std::invalid_argument for an empty name, a built-in's name (the kernel names and
/// "diamond_square"), or an empty factory.
void RegisterUpsampler(const std::string& name, UpsamplerFactory factory);

/// Whether an upsampler type is registered under `name` (`RegisterUpsampler`).
bool IsUpsamplerRegistered(const std::string& name);

/// The names upsampler types are registered under, sorted.
std::vector<std::string> RegisteredUpsamplerNames();

/// The upsampler the type registered under `name` makes with `settings`.
/// @throws std::invalid_argument for a name not registered, or a factory that gives null.
std::shared_ptr<const Upsampler> MakeRegisteredUpsampler(const std::string& name,
                                                         const std::string& settings = "{}");

}  // namespace terraingen
