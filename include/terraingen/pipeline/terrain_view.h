// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `TerrainView`, a terrain's heights over one grid, updated as the terrain is edited.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// The heights of a terrain over one grid, for loops that edit the terrain and evaluate it again.
///
/// The view keeps the heights before one layer, the stage; an update after replacing that layer
/// or a later one applies only the layers from the stage on, and an update of an unchanged
/// terrain evaluates nothing. Parts are told apart by identity, and the view keeps those it last
/// evaluated alive. Updates give the bits of `Terrain::Evaluate`; one may run at a time.
class TerrainView {
  public:
    /// A view of `grid` evaluated with `options`, keeping the heights before layer `stage`:
    /// unset for the terrain's last layer, 0 for the source's heights.
    TerrainView(RasterGrid grid, EvaluationOptions options = {},
                std::optional<std::size_t> stage = std::nullopt);

    /// The grid the view covers.
    const RasterGrid& Grid() const {
        return heights_.Grid();
    }

    /// The terrain's heights over the grid, restarting from the kept heights when the surface,
    /// the source and the layers before the stage are those of the last update; valid until the
    /// next update or `Clear`.
    /// @throws std::invalid_argument for a stage past the terrain's layer count, or as
    /// `Terrain::Evaluate`.
    /// @throws std::runtime_error as `Terrain::Evaluate`.
    const Raster<float>& Update(const Terrain& terrain);

    /// Drops the kept heights and parts, so the next update evaluates the whole terrain.
    void Clear();

  private:
    /// How many layers at the front of `terrain` are those of the last update; unset when there
    /// was none or the surface, source or backend changed.
    std::optional<std::size_t> SameLayers(const Terrain& terrain, Backend backend) const;

    /// Evaluates `terrain` from its source, keeping the heights before layer `stage`.
    void EvaluateKeeping(const Terrain& terrain, std::size_t stage);

    EvaluationOptions options_;
    std::optional<std::size_t> stage_;
    Raster<float> heights_;
    std::vector<float> kept_;
    std::size_t keptStage_ = 0;
    bool evaluated_ = false;
    Backend backend_ = Backend::Cpu;
    Surface surface_{};
    std::shared_ptr<const Source> source_;
    std::vector<LayerPtr> layers_;
};

}  // namespace terraingen
