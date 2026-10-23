/// @file
/// @brief Writing crater and boulder lists as CSV or JSON, and boulder models as 3D scenes.
#pragma once

#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <filesystem>
#include <vector>

namespace terraingen {

/// Formats for crater and boulder lists.
enum class ListFormat { Csv, Json };

/// Largest crease angle of boulder models, in degrees.
inline constexpr double kMaxCreaseAngle = 180.0;

/// Writes craters as CSV (columns x, y, diameter, age_gyr, id, manual) or as JSON
/// ({"craters": [{"x": ..., "y": ..., ...}]}), with doubles written exactly. With an attributes
/// entry for every crater, each also gets floor_depth, rim_height, depth_ratio, freshness and
/// complex.
/// @throws std::invalid_argument for `attributes` that are neither empty nor one per crater.
/// @throws std::runtime_error if the file cannot be written.
void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     ListFormat format, const std::vector<CraterAttributes>& attributes = {});

/// Writes boulders as CSV (columns x, y, ground, diameter, height, burial, yaw_deg, lean_deg, id,
/// model, wear, parent, piece, rest, tilt_deg and tilt_azimuth_deg, angles in degrees) or as JSON
/// ({"boulders": [...]}) with the same keys, doubles written exactly and an unknown ground left
/// empty (CSV) or null (JSON).
/// @throws std::runtime_error if the file cannot be written.
void WriteBoulderList(const std::vector<Boulder>& boulders, const std::filesystem::path& path,
                      ListFormat format);

/// Writes boulder models as a scene (`WriteScene`): each shape once with crease normals and its
/// material, drawn once per boulder (glTF), one object per boulder (OBJ) or one merged mesh
/// (STL). On a sphere the world is the east-north-up frame of `site` on the datum sphere
/// (`PlacementAtSite`), so coordinates stay small; `WriteOutputs` uses the grid's center.
/// @param creaseAngle Angle in degrees, from 0 to 180, above which normals are split.
/// @param instanced Whether shared shapes are written once and drawn per boulder; otherwise each
/// boulder is written as its own mesh, already placed, for tools without instancing.
/// @throws std::invalid_argument for no boulders, an angle outside [0, 180] or as `WriteScene`.
/// @throws std::runtime_error if the file cannot be written.
void WriteBoulderModels(const std::vector<BoulderModel>& boulders,
                        const std::filesystem::path& path, double creaseAngle = 60.0,
                        const Surface& surface = {}, const Vec2d& site = {}, bool instanced = true);

}  // namespace terraingen
