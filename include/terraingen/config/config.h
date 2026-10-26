// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `TerrainGenConfig`, a terrain configuration held as a value, and the limits
/// configurations take.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace terraingen {

/// Version of the configuration format that this library reads and writes.
inline constexpr int kConfigSchemaVersion = 1;

/// Smallest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMinConfigGridSize = 2;

/// Largest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// Largest number of cells along a cube face edge of a body mesh or a shape model's radial grid.
inline constexpr std::size_t kMaxCubeFaceCells = 4096;

/// What GeoTIFF output tile sides, in pixels, must be a multiple of.
inline constexpr std::uint32_t kTileSizeMultiple = 16;

/// What a configuration holds, private to TerrainGen.
struct ConfigSpec;

/// A terrain configuration: the body, source, layers, outputs, albedo and materials its JSON
/// describes (`ParseConfig`, `LoadConfig`), held as an immutable value that is cheap to copy.
class TerrainGenConfig {
  public:
    /// The default configuration: a plane, the default synthetic source, no layers or outputs.
    TerrainGenConfig();

    /// The configuration `spec` describes.
    TerrainGenConfig(ConfigSpec spec);  // NOLINT(google-explicit-constructor)

    /// The folder the configuration was read from (`LoadConfig`), which its relative paths
    /// resolve against when a call gives no folder; empty otherwise.
    const std::filesystem::path& Directory() const;

    /// What the configuration holds.
    const ConfigSpec& Spec() const;

    /// `Spec`, so the configuration passes where its spec is asked for.
    operator const ConfigSpec&() const {  // NOLINT(google-explicit-constructor)
        return Spec();
    }

  private:
    std::shared_ptr<const ConfigSpec> spec_;
};

/// Whether two configurations are the same, as their JSON is (`ConfigToJson`).
bool operator==(const TerrainGenConfig& a, const TerrainGenConfig& b);

/// Whether two configurations differ, as their JSON does (`ConfigToJson`).
bool operator!=(const TerrainGenConfig& a, const TerrainGenConfig& b);

}  // namespace terraingen
