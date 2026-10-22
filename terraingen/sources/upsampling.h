/// @file
/// @brief Upsamplers: how a source's posts become heights at any point and sample spacing.
#pragma once

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"
#include "terraingen/raster/resample.h"
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

/// The posts of a raster that is a window of a larger lattice, such as a window read from a
/// file: posts beyond the larger lattice repeat its edge, and those beyond the window its edge, so
/// reads within the window give the same values as the whole.
class RasterPosts final : public PostLattice {
  public:
    /// @param window A non-empty, unrotated raster; its no-data cells read as NaN.
    /// @param placement Where `window` lies in the whole lattice.
    /// @throws std::invalid_argument for a null or empty window.
    RasterPosts(std::shared_ptr<const Raster<float>> window, const RasterPlacement& placement);

    /// The whole lattice's transform.
    GeoTransform Transform() const override {
        return placement_.whole.transform;
    }

    /// The window's cells in the whole lattice.
    std::optional<PixelWindow> Extent() const override;

    void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
              Span<float> heights) const override;

    /// The window and where it lies.
    const Raster<float>& Window() const {
        return *window_;
    }
    const RasterPlacement& Placement() const {
        return placement_;
    }

  private:
    std::shared_ptr<const Raster<float>> window_;
    RasterPlacement placement_;
};

/// The spacing of a lattice's posts: the longer side of a cell of `transform`, in its units.
double PostSpacing(const GeoTransform& transform);

/// How a source's posts become heights at any point and at any sample spacing in the posts'
/// units: interpolation, or refinement that adds detail as the spacing shrinks. Heights must
/// depend only on the posts, the position and the spacing, and implementations must be
/// thread-safe.
///
/// Sources give a post's own height at the posts' spacing or coarser, so an upsampler only
/// decides heights off the posts and below their spacing.
class Upsampler {
  public:
    virtual ~Upsampler() = default;

    /// The height at world (`x`, `y`) from `posts` at a sample spacing of `spacing`, NaN where it
    /// has none.
    virtual float HeightAt(const PostLattice& posts, double x, double y, double spacing) const = 0;

    /// Sets each cell of `heights` to `HeightAt` its center, to the same bits; the default calls
    /// `HeightAt` cell by cell.
    virtual void Fill(const PostLattice& posts, Raster<float>& heights, double spacing) const;

    /// Posts either side of a point that `HeightAt` reads, so that a source reading a window of
    /// its posts reads enough around it: 2 by default.
    virtual int Reach() const {
        return 2;
    }

    /// An upsampler for `posts` with any settings this one leaves to be estimated from them filled
    /// in, such as diamond-square's roughness; null when this one serves as it is, the default.
    virtual std::shared_ptr<const Upsampler> FitTo(const PostLattice& /*posts*/) const {
        return nullptr;
    }
};

/// Whether `spacing` is no finer than the posts of a lattice placed by `transform`
/// (`PostSpacing`), to within rounding.
bool AtPostSpacing(const GeoTransform& transform, double spacing);

/// The height a source with `posts` gives at (`x`, `y`) at a sample spacing of `spacing`: the
/// post's own where (`x`, `y`) is a post and the spacing is no finer than the posts
/// (`AtPostSpacing`), otherwise `upsampler`'s.
float UpsampledHeight(const Upsampler& upsampler, const PostLattice& posts, double x, double y,
                      double spacing);

/// Sets each cell of `heights` to `UpsampledHeight` at its center, to the same bits.
void FillUpsampled(const Upsampler& upsampler, const PostLattice& posts, Raster<float>& heights,
                   double spacing);

/// Interpolation of the posts with a kernel (`ResampleMethod`), at every spacing alike: `Cubic`,
/// the default, gives the same bits as `SampleRaster` of the posts.
class KernelUpsampler final : public Upsampler {
  public:
    explicit KernelUpsampler(ResampleMethod method = ResampleMethod::Cubic) : method_(method) {}

    /// The posts interpolated at (`x`, `y`) as `SampleRaster` interpolates a raster, NaN where a
    /// post weighed holds no data.
    /// @throws std::invalid_argument for a rotated lattice.
    float HeightAt(const PostLattice& posts, double x, double y, double spacing) const override;

    /// Reads the posts once per block of cells.
    /// @throws std::invalid_argument for a rotated lattice.
    void Fill(const PostLattice& posts, Raster<float>& heights, double spacing) const override;

    /// The kernel's radius (`KernelRadius`).
    int Reach() const override {
        return KernelRadius(method_);
    }

    /// The kernel.
    ResampleMethod Method() const {
        return method_;
    }

  private:
    ResampleMethod method_;
};

/// The upsampler sources use unless given one: cubic interpolation, shared.
std::shared_ptr<const Upsampler> DefaultUpsampler();

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
