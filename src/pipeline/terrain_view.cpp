// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/terrain_view.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

TerrainView::TerrainView(RasterGrid grid, EvaluationOptions options,
                         std::optional<std::size_t> stage)
    : options_(std::move(options)), stage_(stage), heights_(grid) {}

std::optional<std::size_t> TerrainView::SameLayers(const Terrain& terrain, Backend backend) const {
    if (!evaluated_ || backend != backend_ || !(terrain.Surface() == surface_) ||
        terrain.Source() != source_) {
        return std::nullopt;
    }
    const std::vector<LayerPtr>& layers = terrain.Layers();
    const auto same =
        std::mismatch(layers.begin(), layers.end(), layers_.begin(), layers_.end()).first;
    return static_cast<std::size_t>(same - layers.begin());
}

const Raster<float>& TerrainView::Update(const Terrain& terrain) {
    const std::size_t count = terrain.Layers().size();
    const std::size_t stage = stage_.value_or(count > 0 ? count - 1 : 0);
    if (stage > count) {
        throw std::invalid_argument("The view keeps the heights before layer " +
                                    std::to_string(stage) + ", but the terrain has " +
                                    std::to_string(count) + " layers");
    }
    const Backend backend = options_.backend.value_or(DefaultBackend());
    const std::optional<std::size_t> same = SameLayers(terrain, backend);
    if (same && *same == count && count == layers_.size()) {
        return heights_;
    }
    evaluated_ = false;
    if (same && *same >= keptStage_) {
        std::copy(kept_.begin(), kept_.end(), heights_.Values().begin());
        terrain.ApplyFrom(keptStage_, heights_, options_);
    } else {
        EvaluateKeeping(terrain, stage);
    }
    evaluated_ = true;
    backend_ = backend;
    surface_ = terrain.Surface();
    source_ = terrain.Source();
    layers_ = terrain.Layers();
    return heights_;
}

void TerrainView::EvaluateKeeping(const Terrain& terrain, std::size_t stage) {
    terrain.Beneath(stage).Fill(Grid(), heights_.Values(), options_);
    const Span<float> values = heights_.Values();
    kept_.assign(values.begin(), values.end());
    keptStage_ = stage;
    terrain.ApplyFrom(stage, heights_, options_);
}

void TerrainView::Clear() {
    evaluated_ = false;
    kept_ = {};
    source_.reset();
    layers_ = {};
}

}  // namespace terraingen
