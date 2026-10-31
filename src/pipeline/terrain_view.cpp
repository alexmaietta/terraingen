// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/terrain_view.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

TerrainView::TerrainView(RasterGrid grid, EvaluationOptions options,
                         std::optional<std::size_t> stage)
    : m_options(std::move(options)), m_stage(stage), m_heights(grid) {}

std::optional<std::size_t> TerrainView::SameLayers(const Terrain& terrain, Backend backend) const {
    if (!m_evaluated || backend != m_backend || !(terrain.Surface() == m_surface) ||
        terrain.Source() != m_source) {
        return std::nullopt;
    }
    const std::vector<LayerPtr>& layers = terrain.Layers();
    const auto same =
        std::mismatch(layers.begin(), layers.end(), m_layers.begin(), m_layers.end()).first;
    return static_cast<std::size_t>(same - layers.begin());
}

const Raster<float>& TerrainView::Update(const Terrain& terrain) {
    const std::size_t count = terrain.Layers().size();
    const std::size_t stage = m_stage.value_or(count > 0 ? count - 1 : 0);
    if (stage > count) {
        throw std::invalid_argument("The view keeps the heights before layer " +
                                    std::to_string(stage) + ", but the terrain has " +
                                    std::to_string(count) + " layers");
    }
    const Backend backend = m_options.backend.value_or(DefaultBackend());
    const std::optional<std::size_t> same = SameLayers(terrain, backend);
    if (same && *same == count && count == m_layers.size()) {
        return m_heights;
    }
    m_evaluated = false;
    if (same && *same >= m_keptStage) {
        std::copy(m_kept.begin(), m_kept.end(), m_heights.Values().begin());
        terrain.ApplyFrom(m_keptStage, m_heights, m_options);
    } else {
        EvaluateKeeping(terrain, stage);
    }
    m_evaluated = true;
    m_backend = backend;
    m_surface = terrain.Surface();
    m_source = terrain.Source();
    m_layers = terrain.Layers();
    return m_heights;
}

void TerrainView::EvaluateKeeping(const Terrain& terrain, std::size_t stage) {
    terrain.Beneath(stage).Fill(Grid(), m_heights.Values(), m_options);
    const Span<float> values = m_heights.Values();
    m_kept.assign(values.begin(), values.end());
    m_keptStage = stage;
    terrain.ApplyFrom(stage, m_heights, m_options);
}

void TerrainView::Clear() {
    m_evaluated = false;
    m_kept = {};
    m_source.reset();
    m_layers = {};
}

}  // namespace terraingen
