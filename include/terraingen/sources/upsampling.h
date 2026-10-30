// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Upsamplers: how a source's posts become heights at any point and sample spacing.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/sample_block.h"
#include "terraingen/util/span.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
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

namespace detail {

/// An inclusive box of lattice indices, columns [`col0`, `col1`] and rows [`row0`, `row1`];
/// empty until it includes a span.
struct IndexBox {
    std::int64_t col0 = std::numeric_limits<std::int64_t>::max();
    std::int64_t row0 = std::numeric_limits<std::int64_t>::max();
    std::int64_t col1 = std::numeric_limits<std::int64_t>::min();
    std::int64_t row1 = std::numeric_limits<std::int64_t>::min();

    /// Grows the box to hold columns [`first`, `last`] and rows [`top`, `bottom`].
    void Include(std::int64_t first, std::int64_t last, std::int64_t top, std::int64_t bottom) {
        col0 = std::min(col0, first);
        col1 = std::max(col1, last);
        row0 = std::min(row0, top);
        row1 = std::max(row1, bottom);
    }

    /// Columns in a non-empty box.
    std::size_t Columns() const {
        return static_cast<std::size_t>(col1 - col0 + 1);
    }

    /// Rows in a non-empty box.
    std::size_t Rows() const {
        return static_cast<std::size_t>(row1 - row0 + 1);
    }

    /// The number of indices the box holds, as a double so that it cannot overflow.
    double Area() const {
        return static_cast<double>(col1 - col0 + 1) * static_cast<double>(row1 - row0 + 1);
    }
};

/// Values over an `IndexBox` of a lattice, row by row.
template <typename T> struct LatticeWindow {
    IndexBox box;
    std::size_t width = 0;
    std::vector<T> values;

    /// The window over a non-empty `over`, every value `T{}`.
    explicit LatticeWindow(const IndexBox& over)
        : box(over), width(over.Columns()), values(over.Columns() * over.Rows()) {}

    T& At(std::int64_t col, std::int64_t row) {
        return values[static_cast<std::size_t>(row - box.row0) * width +
                      static_cast<std::size_t>(col - box.col0)];
    }
    const T& At(std::int64_t col, std::int64_t row) const {
        return values[static_cast<std::size_t>(row - box.row0) * width +
                      static_cast<std::size_t>(col - box.col0)];
    }
};

/// The posts of `posts` over `box`, NaN where they have no data.
LatticeWindow<float> ReadPosts(const PostLattice& posts, const IndexBox& box);

/// The index along an axis of the post a lattice read takes for index `at`: wrapped into
/// [0, `count`) when `wrap`, as longitudes around a body are, then clamped to [`begin`, `end`),
/// so reads beyond the posts held repeat their edge.
inline std::size_t PostIndex(std::int64_t at, bool wrap, std::size_t count, std::size_t begin,
                             std::size_t end) {
    const auto n = static_cast<std::int64_t>(count);
    const std::int64_t wrapped = wrap ? ((at % n) + n) % n : at;
    return static_cast<std::size_t>(std::clamp<std::int64_t>(
        wrapped, static_cast<std::int64_t>(begin), static_cast<std::int64_t>(end) - 1));
}

/// Where a grid on a lattice starts, in lattice cells.
struct LatticeOffset {
    std::int64_t col = 0;
    std::int64_t row = 0;
};

/// The offset of a grid with transform `grid` on a lattice placed by `lattice`, if its cells are
/// the lattice's cells: the same size and rotation, starting a whole number of cells away.
std::optional<LatticeOffset> OffsetOnLattice(const GeoTransform& lattice, const GeoTransform& grid);

/// Cells per side of the tiles `FillByTiles` hands out.
inline constexpr std::size_t kFillTileCells = 128;

/// Sets the samples of `samples` by `fill(positions, values)`, which sets `values`, one per
/// position: a grid tile by tile, tiles split over threads, each tile's cell centers row by row;
/// points all at once.
/// @param threads Threads to fill a grid over; 0 for every hardware thread.
template <typename FillPoints>
void FillByTiles(SampleBlock& samples, unsigned threads, const FillPoints& fill) {
    Raster<float>* grid = samples.Grid();
    if (grid == nullptr) {
        fill(samples.Points(), samples.Heights());
        return;
    }
    Raster<float>& heights = *grid;
    ForEachTile(heights.Width(), heights.Height(), kFillTileCells, threads,
                [&](const PixelWindow& tile) {
                    const std::size_t width = tile.colEnd - tile.colBegin;
                    std::vector<Vec2d> centers;
                    centers.reserve(width * (tile.rowEnd - tile.rowBegin));
                    for (std::size_t row = tile.rowBegin; row < tile.rowEnd; ++row) {
                        for (std::size_t col = tile.colBegin; col < tile.colEnd; ++col) {
                            centers.push_back(heights.CellCenter(col, row));
                        }
                    }
                    std::vector<float> values(centers.size());
                    fill(Span<const Vec2d>(centers), Span<float>(values));
                    for (std::size_t row = tile.rowBegin; row < tile.rowEnd; ++row) {
                        std::copy_n(values.begin() +
                                        static_cast<std::ptrdiff_t>((row - tile.rowBegin) * width),
                                    width, heights.Row(row).begin() + tile.colBegin);
                    }
                });
}

/// Sets `heights`, one per point of `points`, by `expand(box, points, heights)`: once for them
/// all when the box `readBox(points)` gives holds at most `perPoint` indices per point, so points
/// close together read the lattice once, otherwise point by point with each point's own box.
template <typename ReadBox, typename Expand>
void ExpandPoints(Span<const Vec2d> points, Span<float> heights, double perPoint,
                  const ReadBox& readBox, const Expand& expand) {
    if (points.empty()) {
        return;
    }
    const IndexBox box = readBox(points);
    if (box.Area() <= perPoint * static_cast<double>(points.size())) {
        expand(box, points, heights);
        return;
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Span<const Vec2d> point = points.subspan(i, 1);
        expand(readBox(point), point, heights.subspan(i, 1));
    }
}

}  // namespace detail

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
    /// the posts' lattice (`detail::OffsetOnLattice`) at their spacing or coarser copies them.
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

/// Gives the NaN cells of `heights`, filled from a source with no-data value `noData`, that value
/// (NaN without one), and `heights` the no-data value that leaves (`FilledNoData`).
void MarkMissing(Raster<float>& heights, std::optional<float> noData);

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
    /// post weighed holds no data. Points close together read the posts once
    /// (`detail::ExpandPoints`), reusing a row's kernel weights in the rows below that share them.
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
