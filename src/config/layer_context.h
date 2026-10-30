// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief What checking and building a configuration's layers reads: the outputs' extents, what
/// the layers before found, and what each layer is built on.
#pragma once

#include "config/common.h"

#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/layer.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/terrain_base.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace terraingen::config {

/// Where an output evaluates the layers: the area it covers and its sample spacing.
struct OutputExtent {
    double area = 0.0;     ///< Square meters.
    double spacing = 0.0;  ///< Meters.
    bool listed = false;   ///< A feature list, which holds every boulder however small.
};

/// What a layer is checked against, and what the layers before it found.
struct LayerChecks {
    const std::vector<OutputExtent>& extents;
    bool posts = false;     ///< Whether the source has posts to estimate settings from.
    bool craters = false;   ///< Whether a crater layer came before.
    bool boulders = false;  ///< Whether a boulder layer came before.
    /// Craters the crater layers so far are expected to generate over each extent; empty when none
    /// generate.
    std::vector<double> expectedCraters{};
};

/// What a layer is built on: the surface and measured source, the top-level crater layers so far
/// (all for boulder ejecta, the last for secondaries), the last boulder layer (for sand shadows).
struct LayerInputs {
    /// The surface, and the source's measured data that crater and boulder layers leave out what
    /// it resolves of (null for none); other layers read the terrain's source when evaluated.
    TerrainBase base;
    const std::filesystem::path& directory;
    const std::vector<std::shared_ptr<const CraterLayer>>& craters;
    const std::shared_ptr<const BoulderLayer>& boulders;
    /// The source the configuration gives; null without one.
    const std::shared_ptr<const Source>& source;
    /// The secondaries later layers throw from a crater layer built with these inputs.
    std::vector<SecondaryCraterOptions> thrown{};
};

/// Checks a layer spec before any layer is built; most specs are checked as they are built.
template <typename Spec> void CheckSpec(const Spec&, const std::string&, LayerChecks&) {}

/// Checks what a layer needs built to check, `built` being the layer the spec built.
template <typename Spec>
void CheckBuilt(const Spec&, const Layer& /*built*/, const std::string&, LayerChecks&) {}

/// Requires a source with posts for a layer at `path` that estimates its settings from it.
/// @throws ConfigError at the layer's "estimate_from_source" without one.
void CheckEstimateSource(bool estimate, const std::string& path, const LayerChecks& checks);

}  // namespace terraingen::config
