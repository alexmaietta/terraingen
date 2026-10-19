#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/overloaded.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace terraingen {

namespace {

using Json = nlohmann::ordered_json;

/// One value of a feature list: a CSV cell and a JSON value.
using Cell = std::variant<double, std::optional<double>, std::uint64_t, int, bool>;

/// A column of a feature list: its name and how a row gives its value.
template <typename Row> struct Column {
    const char* name;
    Cell (*get)(const Row&);
};

/// The columns of every crater.
const std::array<Column<Crater>, 6> kCraterColumns{{
    {"x", [](const Crater& c) { return Cell(c.x); }},
    {"y", [](const Crater& c) { return Cell(c.y); }},
    {"diameter", [](const Crater& c) { return Cell(c.diameter); }},
    {"age_gyr", [](const Crater& c) { return Cell(c.ageGyr); }},
    {"id", [](const Crater& c) { return Cell(c.id); }},
    {"manual", [](const Crater& c) { return Cell(c.manual); }},
}};

/// The columns added for craters with attributes.
const std::array<Column<CraterAttributes>, 5> kAttributeColumns{{
    {"floor_depth", [](const CraterAttributes& a) { return Cell(a.floorDepth); }},
    {"rim_height", [](const CraterAttributes& a) { return Cell(a.rimHeight); }},
    {"depth_ratio", [](const CraterAttributes& a) { return Cell(a.depthRatio); }},
    {"freshness", [](const CraterAttributes& a) { return Cell(a.freshness); }},
    {"complex", [](const CraterAttributes& a) { return Cell(a.complex); }},
}};

/// The columns of every boulder.
const std::array<Column<Boulder>, 16> kBoulderColumns{{
    {"x", [](const Boulder& b) { return Cell(b.x); }},
    {"y", [](const Boulder& b) { return Cell(b.y); }},
    {"ground", [](const Boulder& b) { return Cell(b.ground); }},
    {"diameter", [](const Boulder& b) { return Cell(b.diameter); }},
    {"height", [](const Boulder& b) { return Cell(b.height); }},
    {"burial", [](const Boulder& b) { return Cell(b.burial); }},
    {"yaw", [](const Boulder& b) { return Cell(b.yaw); }},
    {"lean", [](const Boulder& b) { return Cell(b.lean); }},
    {"id", [](const Boulder& b) { return Cell(b.id); }},
    {"model", [](const Boulder& b) { return Cell(b.model); }},
    {"wear", [](const Boulder& b) { return Cell(b.wear); }},
    {"parent", [](const Boulder& b) { return Cell(b.parent); }},
    {"piece", [](const Boulder& b) { return Cell(static_cast<std::uint64_t>(b.piece)); }},
    {"rest", [](const Boulder& b) { return Cell(static_cast<int>(b.rest)); }},
    {"tilt", [](const Boulder& b) { return Cell(b.tilt); }},
    {"tilt_azimuth", [](const Boulder& b) { return Cell(b.tiltAzimuth); }},
}};

/// `value` with the digits to read it back exactly.
std::string Number(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    return buffer.data();
}

/// A cell as CSV writes it: doubles exactly, an unknown value empty and booleans as 1 or 0.
std::string CsvText(const Cell& cell) {
    return std::visit(Overloaded{
                          [](double value) { return Number(value); },
                          [](const std::optional<double>& value) {
                              return value ? Number(*value) : std::string();
                          },
                          [](std::uint64_t value) { return std::to_string(value); },
                          [](int value) { return std::to_string(value); },
                          [](bool value) { return std::string(value ? "1" : "0"); },
                      },
                      cell);
}

/// A cell as JSON writes it, an unknown value null.
Json JsonValue(const Cell& cell) {
    return std::visit(
        Overloaded{
            [](const std::optional<double>& value) { return value ? Json(*value) : Json(nullptr); },
            [](const auto& value) { return Json(value); },
        },
        cell);
}

/// Writes the names of `columns` as CSV cells, the first after `separator` and the rest after ",".
template <typename Row, std::size_t N>
void WriteCsvHeader(std::ostream& file, const std::array<Column<Row>, N>& columns,
                    const char* separator) {
    for (const Column<Row>& column : columns) {
        file << separator << column.name;
        separator = ",";
    }
}

/// Writes the cells of `row` as CSV, the first after `separator` and the rest after ",".
template <typename Row, std::size_t N>
void WriteCsvCells(std::ostream& file, const std::array<Column<Row>, N>& columns, const Row& row,
                   const char* separator) {
    for (const Column<Row>& column : columns) {
        file << separator << CsvText(column.get(row));
        separator = ",";
    }
}

/// Adds the cells of `row` to a JSON object, keyed by column name in order.
template <typename Row, std::size_t N>
void AddJsonCells(Json& entry, const std::array<Column<Row>, N>& columns, const Row& row) {
    for (const Column<Row>& column : columns) {
        entry[column.name] = JsonValue(column.get(row));
    }
}

/// @throws std::runtime_error if `path` cannot be opened.
std::ofstream OpenOutput(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    return file;
}

/// @throws std::runtime_error if writing to `file` failed.
void CheckWritten(const std::ofstream& file, const std::filesystem::path& path) {
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

void WriteCraterCsv(std::ostream& file, const CraterList& craters,
                    const std::vector<CraterAttributes>& attributes) {
    WriteCsvHeader(file, kCraterColumns, "");
    if (!attributes.empty()) {
        WriteCsvHeader(file, kAttributeColumns, ",");
    }
    file << '\n';
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        WriteCsvCells(file, kCraterColumns, craters.At(n), "");
        if (!attributes.empty()) {
            WriteCsvCells(file, kAttributeColumns, attributes[n], ",");
        }
        file << '\n';
    }
}

Json CraterJson(const CraterList& craters, const std::vector<CraterAttributes>& attributes) {
    Json list = Json::array();
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        Json entry = Json::object();
        AddJsonCells(entry, kCraterColumns, craters.At(n));
        if (!attributes.empty()) {
            AddJsonCells(entry, kAttributeColumns, attributes[n]);
        }
        list.push_back(std::move(entry));
    }
    return Json{{"craters", std::move(list)}};
}

void WriteBoulderCsv(std::ostream& file, const std::vector<Boulder>& boulders) {
    WriteCsvHeader(file, kBoulderColumns, "");
    file << '\n';
    for (const Boulder& boulder : boulders) {
        WriteCsvCells(file, kBoulderColumns, boulder, "");
        file << '\n';
    }
}

Json BoulderJson(const std::vector<Boulder>& boulders) {
    Json list = Json::array();
    for (const Boulder& boulder : boulders) {
        Json entry = Json::object();
        AddJsonCells(entry, kBoulderColumns, boulder);
        list.push_back(std::move(entry));
    }
    return Json{{"boulders", std::move(list)}};
}

BoulderPlacement SitePlacement(const BoulderModel& boulder, const Surface& surface,
                               const Vec2d& site) {
    return PlacementAtSite(boulder.placement, {boulder.boulder.x, boulder.boulder.y}, site,
                           surface);
}

/// Writes each shared shape once, with crease normals, and a node per boulder; or, not
/// `instanced`, each boulder's placed shape as its own mesh.
void WriteBoulderScene(const std::vector<BoulderModel>& boulders, const std::filesystem::path& path,
                       double creaseAngle, const Surface& surface, const Vec2d& site,
                       bool instanced) {
    const double radians = creaseAngle * kDegree;
    std::vector<SceneMesh> meshes;
    std::map<const BoulderShape*, std::size_t> indices;
    std::vector<SceneNode> nodes;
    for (const BoulderModel& boulder : boulders) {
        if (!instanced) {
            const BoulderPlacement placement = SitePlacement(boulder, surface, site);
            TriangleMesh placed = boulder.shape->mesh;
            for (Vec3d& p : placed.positions) {
                p = placement.Apply(p);
            }
            ShadedMesh shaded = WithCreases(placed, radians);
            nodes.push_back({meshes.size(), {1.0, 1.0, 1.0}, {}});
            meshes.push_back(
                {std::move(shaded.mesh), std::move(shaded.normals), boulder.shape->material});
            continue;
        }
        auto [found, added] = indices.emplace(boulder.shape.get(), meshes.size());
        if (added) {
            ShadedMesh shaded = WithCreases(boulder.shape->mesh, radians);
            meshes.push_back(
                {std::move(shaded.mesh), std::move(shaded.normals), boulder.shape->material});
        }
        const BoulderPlacement placement = SitePlacement(boulder, surface, site);
        nodes.push_back({found->second, placement.scale, placement.pose});
    }
    WriteScene(path, meshes, nodes);
}

}  // namespace

void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     ListFormat format, const std::vector<CraterAttributes>& attributes) {
    if (!attributes.empty() && attributes.size() != craters.Size()) {
        throw std::invalid_argument("Crater attributes must be empty or one per crater");
    }
    std::ofstream file = OpenOutput(path);
    switch (format) {
    case ListFormat::Csv:
        WriteCraterCsv(file, craters, attributes);
        break;
    case ListFormat::Json:
        file << CraterJson(craters, attributes).dump(2) << '\n';
        break;
    }
    CheckWritten(file, path);
}

void WriteBoulderList(const std::vector<Boulder>& boulders, const std::filesystem::path& path,
                      ListFormat format) {
    std::ofstream file = OpenOutput(path);
    switch (format) {
    case ListFormat::Csv:
        WriteBoulderCsv(file, boulders);
        break;
    case ListFormat::Json:
        file << BoulderJson(boulders).dump(2) << '\n';
        break;
    }
    CheckWritten(file, path);
}

void WriteBoulderModels(const std::vector<BoulderModel>& boulders,
                        const std::filesystem::path& path, double creaseAngle,
                        const Surface& surface, const Vec2d& site, bool instanced) {
    if (!(creaseAngle >= 0.0 && creaseAngle <= kMaxCreaseAngle)) {
        throw std::invalid_argument("Crease angle must be between 0 and 180 degrees");
    }
    if (boulders.empty()) {
        throw std::invalid_argument("There are no boulder models to write");
    }
    WriteBoulderScene(boulders, path, creaseAngle, surface, site, instanced);
}

}  // namespace terraingen
